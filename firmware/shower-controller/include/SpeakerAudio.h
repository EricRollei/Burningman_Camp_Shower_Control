#pragma once

#include <Arduino.h>
#include <BluetoothA2DPSource.h>
#include <FS.h>

class SpeakerAudio {
 public:
  bool begin();
  void handle();

  bool playSong();
  bool playTestTone();
  void stop();

  bool connected() const;
  bool playing() const;
  bool fileAvailable() const;
  uint32_t bytesPlayed() const { return bytesPlayed_; }
  const char* connectionLabel() const;
  const char* playbackLabel() const;

 private:
  enum class Mode : uint8_t { Silence, Tone, Song };

  static int32_t provideAudio(uint8_t* data, int32_t length);
  static void connectionChanged(esp_a2d_connection_state_t state, void* context);
  static bool selectSpeaker(const char* name, esp_bd_addr_t address, int rssi);
  int32_t fillAudio(uint8_t* data, int32_t length);

  static SpeakerAudio* instance_;
  BluetoothA2DPSource source_;
  File songFile_;
  volatile Mode mode_ = Mode::Silence;
  volatile esp_a2d_connection_state_t connectionState_ =
      ESP_A2D_CONNECTION_STATE_DISCONNECTED;
  volatile uint32_t toneFramesRemaining_ = 0;
  volatile uint32_t bytesPlayed_ = 0;
  volatile bool playbackEnded_ = false;
  uint32_t closeFileAfterMs_ = 0;
  bool started_ = false;
};

