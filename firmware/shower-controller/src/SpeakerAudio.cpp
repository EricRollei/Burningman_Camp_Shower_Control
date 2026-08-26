#include "SpeakerAudio.h"

#include <SD.h>
#include <cstring>

#include "Config.h"

SpeakerAudio* SpeakerAudio::instance_ = nullptr;

bool SpeakerAudio::begin() {
  if (started_) return true;
  instance_ = this;
  source_.set_local_name("Camp Shower Controller");
  source_.set_ssp_enabled(true);
  source_.set_auto_reconnect(true);
  setSpeakerVolumePercent(speakerVolumePercent_);
  source_.set_on_connection_state_changed(connectionChanged, this);
  source_.set_ssid_callback(selectSpeaker);
  source_.start_raw(provideAudio);
  started_ = true;
  Serial.println("[AUDIO] Bluetooth source started; looking for Select 4 Go");
  return true;
}

void SpeakerAudio::handle() {
  if (playbackEnded_) {
    playbackEnded_ = false;
    if (songFile_) songFile_.close();
    Serial.printf("[AUDIO] song finished bytes=%lu\n",
                  static_cast<unsigned long>(bytesPlayed_));
  }
  if (closeFileAfterMs_ != 0 &&
      static_cast<int32_t>(millis() - closeFileAfterMs_) >= 0) {
    closeFileAfterMs_ = 0;
    if (songFile_) songFile_.close();
  }
}

bool SpeakerAudio::playSong() {
  if (!connected()) return false;
  stop();
  delay(120);
  handle();
  songFile_ = SD.open(Config::AUDIO_PATH, FILE_READ);
  if (!songFile_) return false;
  channel_ = 1;
  bytesPlayed_ = 0;
  playbackEnded_ = false;
  mode_ = Mode::Song;
  Serial.printf("[AUDIO] playing %s bytes=%lu\n", Config::AUDIO_PATH,
                static_cast<unsigned long>(songFile_.size()));
  return true;
}

bool SpeakerAudio::playChannel(uint8_t channel) {
  if (!connected() || channel == 0 ||
      channel >= Config::MUSIC_KNOB_POSITION_COUNT) return false;
  stop();
  delay(120);
  handle();
  const char* path = Config::MUSIC_CHANNEL_PATHS[channel];
  songFile_ = SD.open(path, FILE_READ);
  if (!songFile_) return false;
  channel_ = channel;
  bytesPlayed_ = 0;
  playbackEnded_ = false;
  mode_ = Mode::Song;
  Serial.printf("[AUDIO] channel=%u name=%s playing %s\n", channel,
                Config::MUSIC_CHANNEL_NAMES[channel], path);
  return true;
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
  mode_ = Mode::Silence;
  toneFramesRemaining_ = 0;
  if (songFile_) closeFileAfterMs_ = millis() + 100;
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

bool SpeakerAudio::fileAvailable() const {
  for (uint8_t channel = 1; channel < Config::MUSIC_KNOB_POSITION_COUNT;
       ++channel) {
    if (!SD.exists(Config::MUSIC_CHANNEL_PATHS[channel])) return false;
  }
  return true;
}

const char* SpeakerAudio::connectionLabel() const {
  switch (connectionState_) {
    case ESP_A2D_CONNECTION_STATE_CONNECTED: return "connected";
    case ESP_A2D_CONNECTION_STATE_CONNECTING: return "connecting";
    case ESP_A2D_CONNECTION_STATE_DISCONNECTING: return "disconnecting";
    default: return started_ ? "searching" : "disabled";
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

  if (mode_ == Mode::Song && songFile_) {
    const size_t read = songFile_.read(data, static_cast<size_t>(length));
    analyzePcm(data, read);
    bytesPlayed_ += read;
    if (read < static_cast<size_t>(length)) {
      mode_ = Mode::Silence;
      playbackEnded_ = true;
    }
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
