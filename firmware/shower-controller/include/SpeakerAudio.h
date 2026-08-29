#pragma once

#include <Arduino.h>
#include <BluetoothA2DPSource.h>
#include <FS.h>

#include "Config.h"

// The stock source restarts GAP inquiry forever when no speaker is found,
// which starves the WiFi AP. This subclass lets the main loop hold discovery
// after the current round and resume it on a slow interval or on demand.
class ShowerA2DPSource : public BluetoothA2DPSource {
 public:
  void holdDiscovery() { discoveryHold_ = true; }
  bool discoveryHeld() const { return discoveryHeldNow_; }
  bool resumeDiscovery();

 protected:
  void bt_app_gap_callback(esp_bt_gap_cb_event_t event,
                           esp_bt_gap_cb_param_t* param) override;

 private:
  volatile bool discoveryHold_ = false;
  volatile bool discoveryHeldNow_ = false;
};

class SpeakerAudio {
 public:
  bool begin();
  void handle();

  bool playSong();
  bool playChannel(uint8_t channel);
  bool startRadioStatic();
  bool playTestTone();
  void stop();
  void setSpeakerVolumePercent(uint8_t percent);
  // Re-scan the SD card for channel files (main task only; call after upload).
  void refreshFileAvailability();
  // Ask for a fresh Bluetooth discovery window (admin "find speaker" button).
  void requestDiscovery();

  bool connected() const;
  bool playing() const;
  bool songPlaying() const;
  bool fileAvailable() const { return filesAvailable_; }
  uint32_t bytesPlayed() const { return bytesPlayed_; }
  uint32_t playbackPositionMs() const;
  uint32_t bufferUnderruns() const { return bufferUnderruns_; }
  uint8_t channel() const { return channel_; }
  uint8_t bassLevel() const { return bassLevel_; }
  uint8_t midLevel() const { return midLevel_; }
  uint8_t trebleLevel() const { return trebleLevel_; }
  uint8_t volumeLevel() const { return volumeLevel_; }
  uint8_t speakerVolumePercent() const { return speakerVolumePercent_; }
  const char* connectionLabel() const;
  const char* playbackLabel() const;

 private:
  enum class Mode : uint8_t { Silence, Tone, RadioStatic, Song };

  static int32_t provideAudio(uint8_t* data, int32_t length);
  static void connectionChanged(esp_a2d_connection_state_t state, void* context);
  static bool selectSpeaker(const char* name, esp_bd_addr_t address, int rssi);
  int32_t fillAudio(uint8_t* data, int32_t length);
  void analyzePcm(const uint8_t* data, size_t length);
  bool openSong(const char* path, uint8_t channel);
  void refillBuffer();
  void resetBuffer();
  void serviceDiscoveryBackoff(uint32_t now);
  size_t bufferedBytes() const;

  static SpeakerAudio* instance_;
  ShowerA2DPSource source_;
  File songFile_;

  // Single-producer (main loop) / single-consumer (Bluetooth task) ring
  // buffer. Monotonic 32-bit head/tail; aligned loads/stores are atomic on
  // ESP32 and each index has exactly one writer.
  uint8_t* ringBuffer_ = nullptr;
  size_t ringSize_ = 0;
  volatile uint32_t ringHead_ = 0;  // written by main loop
  volatile uint32_t ringTail_ = 0;  // written by Bluetooth task
  volatile bool fileExhausted_ = true;
  volatile bool consumerBusy_ = false;
  volatile uint32_t bufferUnderruns_ = 0;

  volatile Mode mode_ = Mode::Silence;
  volatile esp_a2d_connection_state_t connectionState_ =
      ESP_A2D_CONNECTION_STATE_DISCONNECTED;
  volatile uint32_t toneFramesRemaining_ = 0;
  volatile uint32_t bytesPlayed_ = 0;
  volatile bool playbackEnded_ = false;
  volatile uint8_t bassLevel_ = 0;
  volatile uint8_t midLevel_ = 0;
  volatile uint8_t trebleLevel_ = 0;
  volatile uint8_t volumeLevel_ = 0;
  uint32_t noiseState_ = 0x8E37A91BU;
  int32_t bassFilter_ = 0;
  int32_t midFilter_ = 0;
  uint32_t bassPeak_ = 256;
  uint32_t midPeak_ = 256;
  uint32_t treblePeak_ = 256;
  uint32_t volumePeak_ = 256;
  uint8_t channel_ = 0;
  uint8_t speakerVolumePercent_ = Config::DEFAULT_SPEAKER_VOLUME_PERCENT;
  bool filesAvailable_ = false;
  uint32_t discoveryWindowStartMs_ = 0;
  uint32_t lastDiscoveryRetryMs_ = 0;
  bool started_ = false;
};
