#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "Config.h"

class SpeakerAudio;

class LightShow {
 public:
  enum class Effect : uint8_t {
    Glow,
    Wave,
    FrequencyWaves,
    BeatBreathing,
    RhythmRipples,
    BeatChase,
    SpectrumBands,
    PurpleStorm,
    BeatPulse,
    Rain,
    Sparkle,
    Sunrise,
    DrumRipple,
    BootStomp,
    DiscoBlocks,
    MirrorBall,
    NeonSweep,
    KickChase,
    LaserChase,
    IndustrialSparks,
    Firestorm,
    Finale,
    FadeOut,
  };

  struct Cue {
    uint32_t startMs;
    Effect effect;
    uint8_t intensity;
    const char* name;
  };

  struct Show {
    uint8_t channel;
    const char* name;
    const Cue* cues;
    uint8_t cueCount;
    uint32_t endMs;
    uint16_t beatMs;
    uint16_t beatPhaseMs;
    uint8_t primaryHue;
    uint8_t secondaryHue;
    uint8_t accentHue;
    uint8_t saturation;
  };

  void begin();
  void handle(const SpeakerAudio& audio, bool sessionActive);

 private:
  void renderRainbow();
  void renderShow(const Show& show, uint32_t songMs);
  void renderCue(const Show& show, const Cue& cue, uint32_t songMs);
  uint8_t beatPulse(const Show& show, uint32_t songMs) const;
  static uint16_t purpleBeatIndex(uint32_t songMs);
  static uint32_t purpleBeatMs(uint16_t index);
  static uint8_t hash8(uint16_t pixel, uint32_t frame);
  static CRGB color(uint8_t hue, uint8_t saturation, uint8_t value);

  CRGB leds_[Config::LED_STRIP_COUNT];
  uint32_t lastFrameMs_ = 0;
  uint8_t rainbowOffset_ = 0;
  bool sessionLightingActive_ = false;
  int8_t activeShowChannel_ = -1;
  int8_t lastCue_ = -1;
  uint8_t bass_ = 0;
  uint8_t mid_ = 0;
  uint8_t treble_ = 0;
  uint8_t volume_ = 0;
};
