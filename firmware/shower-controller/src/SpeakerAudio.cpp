#include "SpeakerAudio.h"

#include <SD.h>
#include <esp_gap_bt_api.h>
#include <cstring>

#include "Config.h"

SpeakerAudio* SpeakerAudio::instance_ = nullptr;

void ShowerA2DPSource::bt_app_gap_callback(esp_bt_gap_cb_event_t event,
                                           esp_bt_gap_cb_param_t* param) {
  // Swallow only the "not found, scan again" restart; the discovered→connect
  // transition and every other event pass through untouched.
  if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT &&
      param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED &&
      s_a2d_state == APP_AV_STATE_DISCOVERING && discoveryHold_) {
    discoveryHeldNow_ = true;
    ESP_LOGI("SHOWER_BT", "discovery held; WiFi gets the radio");
    return;
  }
  BluetoothA2DPSource::bt_app_gap_callback(event, param);
}

void ShowerA2DPSource::bt_av_hdl_avrc_ct_evt(uint16_t event, void* param) {
#ifdef ESP_IDF_4
  auto* rc = static_cast<esp_avrc_ct_cb_param_t*>(param);
  if (event == ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT) {
    // Speaker volume is controlled by its physical buttons. Remove volume
    // notifications before the upstream source sees the capabilities so it
    // neither subscribes to them nor sends its automatic volume+5 response.
    esp_avrc_rn_evt_bit_mask_operation(
        ESP_AVRC_BIT_MASK_OP_CLEAR, &rc->get_rn_caps_rsp.evt_set,
        ESP_AVRC_RN_VOLUME_CHANGE);
  } else if (event == ESP_AVRC_CT_CHANGE_NOTIFY_EVT &&
             rc->change_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
    return;
  }
#endif
  BluetoothA2DPSource::bt_av_hdl_avrc_ct_evt(event, param);
}

bool ShowerA2DPSource::resumeDiscovery() {
  if (!discoveryHeldNow_) return false;
  discoveryHeldNow_ = false;
  s_a2d_state = APP_AV_STATE_DISCOVERING;
  esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
  return true;
}

bool SpeakerAudio::begin() {
  if (started_) return true;
  instance_ = this;
  ringBuffer_ = static_cast<uint8_t*>(
      ps_malloc(Config::AUDIO_BUFFER_BYTES));
  ringSize_ = Config::AUDIO_BUFFER_BYTES;
  if (ringBuffer_ == nullptr) {
    // No PSRAM: a smaller internal-RAM buffer still keeps the Bluetooth task
    // off the SD card, which is the property that matters.
    ringSize_ = 16384;
    ringBuffer_ = static_cast<uint8_t*>(malloc(ringSize_));
  }
  if (ringBuffer_ == nullptr) {
    ringSize_ = 0;
    Serial.println("[AUDIO] buffer allocation failed; song playback disabled");
  }
  refreshFileAvailability();
  source_.set_local_name("Camp Shower Controller");
  source_.set_ssp_enabled(true);
  source_.set_auto_reconnect(true);
  setSpeakerVolumePercent(speakerVolumePercent_);
  source_.set_on_connection_state_changed(connectionChanged, this);
  source_.set_ssid_callback(selectSpeaker);
  source_.start_raw(provideAudio);
  started_ = true;
  discoveryWindowStartMs_ = millis();
  Serial.printf("[AUDIO] Bluetooth source started; buffer=%u bytes; looking for Select 4 Go\n",
                static_cast<unsigned>(ringSize_));
  return true;
}

size_t SpeakerAudio::bufferedBytes() const {
  return static_cast<uint32_t>(ringHead_ - ringTail_);
}

void SpeakerAudio::resetBuffer() {
  // Only call with mode_ == Silence so the consumer is not popping.
  ringTail_ = 0;
  ringHead_ = 0;
}

void SpeakerAudio::refillBuffer() {
  // No mode_ check: openSong() primes the ring before mode_ becomes Song, and
  // a stopped song has no open file, so songFile_ alone gates refills.
  if (!songFile_ || fileExhausted_ || ringSize_ == 0) return;
  size_t budget = Config::AUDIO_REFILL_CHUNK_BYTES;
  while (budget > 0) {
    const size_t buffered = bufferedBytes();
    if (buffered >= ringSize_) break;
    const size_t space = ringSize_ - buffered;
    const size_t offset = ringHead_ % ringSize_;
    const size_t contiguous = min(space, ringSize_ - offset);
    const size_t want = min(budget, contiguous);
    const size_t read = songFile_.read(ringBuffer_ + offset, want);
    if (read > 0) {
      __sync_synchronize();  // data visible before the head moves
      ringHead_ += read;
      budget -= read;
    }
    if (read < want) {
      fileExhausted_ = true;
      songFile_.close();
      break;
    }
  }
}

void SpeakerAudio::handle() {
  if (playbackEnded_) {
    playbackEnded_ = false;
    if (songFile_) songFile_.close();
    Serial.printf("[AUDIO] song finished bytes=%lu underruns=%lu\n",
                  static_cast<unsigned long>(bytesPlayed_),
                  static_cast<unsigned long>(bufferUnderruns_));
  }
  refillBuffer();
  serviceDiscoveryBackoff(millis());
}

void SpeakerAudio::serviceDiscoveryBackoff(uint32_t now) {
  if (!started_) return;
  if (connected()) {
    // Connected: reset the window so a future disconnect scans promptly.
    discoveryWindowStartMs_ = now;
    return;
  }
  if (now - discoveryWindowStartMs_ >= Config::BT_DISCOVERY_ACTIVE_MS) {
    source_.holdDiscovery();
  }
  if (source_.discoveryHeld() &&
      now - lastDiscoveryRetryMs_ >= Config::BT_DISCOVERY_RETRY_INTERVAL_MS) {
    lastDiscoveryRetryMs_ = now;
    if (source_.resumeDiscovery())
      Serial.println("[AUDIO] periodic speaker discovery round");
  }
}

void SpeakerAudio::requestDiscovery() {
  if (!started_ || connected()) return;
  discoveryWindowStartMs_ = millis();
  if (source_.resumeDiscovery())
    Serial.println("[AUDIO] discovery resumed by admin request");
}

void SpeakerAudio::refreshFileAvailability() {
  bool available = true;
  for (uint8_t channel = 1; channel < Config::MUSIC_KNOB_POSITION_COUNT;
       ++channel) {
    if (!SD.exists(Config::MUSIC_CHANNEL_PATHS[channel])) {
      available = false;
      break;
    }
  }
  filesAvailable_ = available;
}

bool SpeakerAudio::openSong(const char* path, uint8_t channel) {
  if (!connected() || ringSize_ == 0) return false;
  stop();
  songFile_ = SD.open(path, FILE_READ);
  if (!songFile_) return false;
  channel_ = channel;
  bytesPlayed_ = 0;
  bufferUnderruns_ = 0;
  playbackEnded_ = false;
  fileExhausted_ = false;
  resetBuffer();
  refillBuffer();  // prime before the Bluetooth task starts consuming
  mode_ = Mode::Song;
  Serial.printf("[AUDIO] channel=%u name=%s playing %s bytes=%lu\n", channel,
                Config::MUSIC_CHANNEL_NAMES[channel], path,
                static_cast<unsigned long>(songFile_.size()));
  return true;
}

bool SpeakerAudio::playSong() { return openSong(Config::AUDIO_PATH, 1); }

bool SpeakerAudio::playChannel(uint8_t channel) {
  if (channel == 0 || channel >= Config::MUSIC_KNOB_POSITION_COUNT) return false;
  return openSong(Config::MUSIC_CHANNEL_PATHS[channel], channel);
}

bool SpeakerAudio::startRadioStatic() {
  if (!connected()) return false;
  stop();
  noiseState_ ^= micros();
  mode_ = Mode::RadioStatic;
  Serial.println("[AUDIO] continuous tuning static started");
  return true;
}

bool SpeakerAudio::playTestTone() {
  if (!connected()) return false;
  stop();
  toneFramesRemaining_ = Config::AUDIO_SAMPLE_RATE * 2;
  mode_ = Mode::Tone;
  Serial.println("[AUDIO] playing two-second test tone");
  return true;
}

void SpeakerAudio::stop() {
  const bool wasPlaying = mode_ != Mode::Silence;
  // Silence the consumer, then wait out any pop already in flight on the
  // Bluetooth task before this task closes the file and resets the ring.
  mode_ = Mode::Silence;
  __sync_synchronize();
  const uint32_t waitStart = millis();
  while (consumerBusy_ && millis() - waitStart < 50) delay(1);
  toneFramesRemaining_ = 0;
  fileExhausted_ = true;
  if (songFile_) songFile_.close();
  resetBuffer();
  if (wasPlaying) Serial.println("[AUDIO] playback stopped");
}

void SpeakerAudio::setSpeakerVolumePercent(uint8_t percent) {
  speakerVolumePercent_ = min<uint8_t>(percent, 100);
  // The default ESP32-A2DP curve reaches full scale at 127.
  const uint8_t a2dpVolume = static_cast<uint8_t>(
      (static_cast<uint16_t>(speakerVolumePercent_) * 127U + 50U) / 100U);
  source_.set_volume(a2dpVolume);
  Serial.printf("[AUDIO] volume=%u%% a2dp=%u\n", speakerVolumePercent_,
                a2dpVolume);
}

bool SpeakerAudio::connected() const {
  return connectionState_ == ESP_A2D_CONNECTION_STATE_CONNECTED;
}

bool SpeakerAudio::playing() const { return mode_ != Mode::Silence; }

bool SpeakerAudio::songPlaying() const { return mode_ == Mode::Song; }

uint32_t SpeakerAudio::playbackPositionMs() const {
  constexpr uint32_t bytesPerFrame = 4;  // signed 16-bit stereo PCM
  const uint64_t frames = bytesPlayed_ / bytesPerFrame;
  return static_cast<uint32_t>((frames * 1000ULL) / Config::AUDIO_SAMPLE_RATE);
}

const char* SpeakerAudio::connectionLabel() const {
  switch (connectionState_) {
    case ESP_A2D_CONNECTION_STATE_CONNECTED: return "connected";
    case ESP_A2D_CONNECTION_STATE_CONNECTING: return "connecting";
    case ESP_A2D_CONNECTION_STATE_DISCONNECTING: return "disconnecting";
    default:
      if (!started_) return "disabled";
      return source_.discoveryHeld() ? "search paused" : "searching";
  }
}

const char* SpeakerAudio::playbackLabel() const {
  switch (mode_) {
    case Mode::Tone: return "test tone";
    case Mode::RadioStatic: return "tuning static";
    case Mode::Song: return "playing song";
    default: return "idle";
  }
}

int32_t SpeakerAudio::provideAudio(uint8_t* data, int32_t length) {
  if (instance_ == nullptr) {
    memset(data, 0, length);
    return length;
  }
  return instance_->fillAudio(data, length);
}

void SpeakerAudio::connectionChanged(esp_a2d_connection_state_t state,
                                     void* context) {
  auto* self = static_cast<SpeakerAudio*>(context);
  if (self == nullptr) return;
  self->connectionState_ = state;
  Serial.printf("[AUDIO] speaker %s\n", self->source_.to_str(state));
}

bool SpeakerAudio::selectSpeaker(const char* name, esp_bd_addr_t, int rssi) {
  if (name == nullptr) return false;
  String candidate(name);
  candidate.toLowerCase();
  const bool match = candidate.indexOf("select 4 go") >= 0;
  if (match) Serial.printf("[AUDIO] found speaker name=%s rssi=%d\n", name, rssi);
  return match;
}

int32_t SpeakerAudio::fillAudio(uint8_t* data, int32_t length) {
  memset(data, 0, length);
  if (length <= 0) return 0;

  if (mode_ == Mode::Song) {
    // Announce the pop before re-checking mode_ so stop() (which sets Silence
    // and then waits for consumerBusy_) can never reset the ring mid-copy.
    consumerBusy_ = true;
    __sync_synchronize();
    if (mode_ != Mode::Song) {
      consumerBusy_ = false;
      return length;
    }
    // Consume from the RAM ring only — this task must never touch the SD card.
    size_t taken = 0;
    while (taken < static_cast<size_t>(length)) {
      const size_t available = bufferedBytes();
      if (available == 0) break;
      const size_t offset = ringTail_ % ringSize_;
      const size_t contiguous = min(available, ringSize_ - offset);
      const size_t want = min(static_cast<size_t>(length) - taken, contiguous);
      memcpy(data + taken, ringBuffer_ + offset, want);
      __sync_synchronize();  // finish the copy before releasing the space
      ringTail_ += want;
      taken += want;
    }
    analyzePcm(data, taken);
    bytesPlayed_ += taken;
    if (taken < static_cast<size_t>(length)) {
      if (fileExhausted_) {
        mode_ = Mode::Silence;
        playbackEnded_ = true;
      } else {
        ++bufferUnderruns_;
      }
    }
    consumerBusy_ = false;
    return length;
  }

  if (mode_ == Mode::RadioStatic) {
    int16_t* samples = reinterpret_cast<int16_t*>(data);
    const int32_t frames = length / 4;
    for (int32_t frame = 0; frame < frames; ++frame) {
      // Xorshift noise makes convincing continuous radio-tuning static without
      // storing another audio asset on the SD card.
      noiseState_ ^= noiseState_ << 13;
      noiseState_ ^= noiseState_ >> 17;
      noiseState_ ^= noiseState_ << 5;
      const int16_t value =
          static_cast<int16_t>((noiseState_ & 0x1FFFU) - 4096);
      samples[frame * 2] = value;
      samples[frame * 2 + 1] = value;
    }
    return length;
  }

  if (mode_ == Mode::Tone) {
    int16_t* samples = reinterpret_cast<int16_t*>(data);
    const int32_t frames = length / 4;
    static uint32_t phase = 0;
    for (int32_t frame = 0; frame < frames; ++frame) {
      int16_t value = 0;
      if (toneFramesRemaining_ > 0) {
        // 882 Hz square wave at moderate amplitude; integer-only and cheap.
        value = ((phase / 25) & 1U) ? 7000 : -7000;
        ++phase;
        --toneFramesRemaining_;
      }
      samples[frame * 2] = value;
      samples[frame * 2 + 1] = value;
    }
    if (toneFramesRemaining_ == 0) mode_ = Mode::Silence;
  }

  return length;
}

void SpeakerAudio::analyzePcm(const uint8_t* data, size_t length) {
  // Three inexpensive IIR bands analyze the exact PCM sent to Bluetooth. This
  // provides stable, zero-room-noise animation levels without an FFT or mic.
  const int16_t* samples = reinterpret_cast<const int16_t*>(data);
  const size_t frames = length / 4U;
  if (frames == 0) return;

  uint64_t bassTotal = 0;
  uint64_t midTotal = 0;
  uint64_t trebleTotal = 0;
  uint64_t volumeTotal = 0;
  for (size_t frame = 0; frame < frames; ++frame) {
    const int32_t mono =
        (static_cast<int32_t>(samples[frame * 2U]) +
         static_cast<int32_t>(samples[frame * 2U + 1U])) /
        2;
    // Approximate splits near 220 Hz and 1.8 kHz at 44.1 kHz.
    bassFilter_ += (mono - bassFilter_) / 32;
    midFilter_ += (mono - midFilter_) / 4;
    bassTotal += static_cast<uint32_t>(abs(bassFilter_));
    midTotal += static_cast<uint32_t>(abs(midFilter_ - bassFilter_));
    trebleTotal += static_cast<uint32_t>(abs(mono - midFilter_));
    volumeTotal += static_cast<uint32_t>(abs(mono));
  }

  const uint32_t bass = bassTotal / frames;
  const uint32_t mid = midTotal / frames;
  const uint32_t treble = trebleTotal / frames;
  const uint32_t volume = volumeTotal / frames;

  auto normalize = [](uint32_t value, uint32_t& peak,
                      volatile uint8_t& smoothed) {
    if (value > peak) {
      peak = value;
    } else if (peak > 256) {
      peak -= max<uint32_t>(1U, peak / 2048U);
    }
    const uint8_t level = static_cast<uint8_t>(
        min<uint32_t>(255U, value * 255U / max<uint32_t>(peak, 1U)));
    smoothed = static_cast<uint8_t>(
        (static_cast<uint16_t>(smoothed) * 3U + level) / 4U);
  };

  normalize(bass, bassPeak_, bassLevel_);
  normalize(mid, midPeak_, midLevel_);
  normalize(treble, treblePeak_, trebleLevel_);
  normalize(volume, volumePeak_, volumeLevel_);
}
