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
  source_.set_volume(Config::SPEAKER_VOLUME);
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
  bytesPlayed_ = 0;
  playbackEnded_ = false;
  mode_ = Mode::Song;
  Serial.printf("[AUDIO] playing %s bytes=%lu\n", Config::AUDIO_PATH,
                static_cast<unsigned long>(songFile_.size()));
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

bool SpeakerAudio::connected() const {
  return connectionState_ == ESP_A2D_CONNECTION_STATE_CONNECTED;
}

bool SpeakerAudio::playing() const { return mode_ != Mode::Silence; }

bool SpeakerAudio::fileAvailable() const { return SD.exists(Config::AUDIO_PATH); }

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
    bytesPlayed_ += read;
    if (read < static_cast<size_t>(length)) {
      mode_ = Mode::Silence;
      playbackEnded_ = true;
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
