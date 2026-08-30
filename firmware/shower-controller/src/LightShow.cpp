#include "LightShow.h"

#include "PurpleRainTiming.h"
#include "SpeakerAudio.h"

namespace {
using Effect = LightShow::Effect;
using Cue = LightShow::Cue;
using Show = LightShow::Show;

// Every cue sheet is matched to the PCM file currently installed on the SD
// card. The effect clock comes from bytes consumed by Bluetooth audio, not
// millis(), so replaying or changing channels always starts in sync.
constexpr Cue kPurpleRain[] = {
    {0, Effect::BeatBreathing, 52, "eight-beat violet breath"},
    {16637, Effect::FrequencyWaves, 66, "three-band velvet waves"},
    {46626, Effect::RhythmRipples, 88, "first purple ripples"},
    {69753, Effect::BeatChase, 82, "alternating violet runners"},
    {76487, Effect::PurpleStorm, 106, "first purple rainstorm"},
    {88445, Effect::SpectrumBands, 78, "purple spectrum verse"},
    {111096, Effect::RhythmRipples, 112, "wide chorus ripples"},
    {130009, Effect::PurpleStorm, 118, "purple rain downpour"},
    {157582, Effect::BeatChase, 122, "anthem runners"},
    {186967, Effect::FrequencyWaves, 108, "guitar frequency waves"},
    {201712, Effect::PurpleStorm, 136, "final purple storm"},
    {224990, Effect::Finale, 146, "ripple chase finale"},
    {231189, Effect::FadeOut, 108, "last violet breath"},
};

constexpr Cue kAfrica[] = {
    {0, Effect::Sunrise, 52, "distant sunrise"},
    {10000, Effect::Wave, 62, "savanna breeze"},
    {32000, Effect::DrumRipple, 78, "talking drums"},
    {52000, Effect::Rain, 70, "first rain"},
    {70000, Effect::BeatPulse, 90, "Africa chorus"},
    {100000, Effect::Wave, 68, "warm horizon"},
    {122000, Effect::Rain, 82, "blessed rain"},
    {135000, Effect::BeatPulse, 104, "chorus sunset"},
    {168000, Effect::DrumRipple, 92, "drum circle"},
    {193000, Effect::Rain, 96, "downpour"},
    {244000, Effect::Finale, 122, "golden chorus"},
    {261000, Effect::Sunrise, 112, "last horizon"},
    {285000, Effect::FadeOut, 90, "sunset fade"},
};

constexpr Cue kWhoseBed[] = {
    {0, Effect::Glow, 55, "honky-tonk glow"},
    {8000, Effect::BootStomp, 72, "boots walking"},
    {27000, Effect::Wave, 68, "country strut"},
    {41000, Effect::BeatPulse, 92, "question chorus"},
    {52000, Effect::BootStomp, 88, "heel-toe"},
    {89000, Effect::Sparkle, 76, "rhinestone wink"},
    {100000, Effect::BeatPulse, 102, "pink chorus"},
    {116000, Effect::BootStomp, 96, "boots again"},
    {145000, Effect::Wave, 80, "sassy strut"},
    {169000, Effect::BeatPulse, 112, "big question"},
    {183000, Effect::Sparkle, 104, "country glitter"},
    {197000, Effect::BootStomp, 116, "dance-floor boots"},
    {256000, Effect::FadeOut, 86, "last wink"},
};

constexpr Cue kItsMyHouse[] = {
    {0, Effect::Glow, 54, "porch light"},
    {11000, Effect::DiscoBlocks, 74, "doors open"},
    {64000, Effect::BeatPulse, 96, "my house chorus"},
    {74000, Effect::MirrorBall, 86, "mirror foyer"},
    {90000, Effect::DiscoBlocks, 92, "room to room"},
    {108000, Effect::BeatPulse, 110, "house party"},
    {117000, Effect::NeonSweep, 94, "hallway sweep"},
    {131000, Effect::MirrorBall, 104, "disco ceiling"},
    {150000, Effect::DiscoBlocks, 108, "block party"},
    {162000, Effect::BeatPulse, 120, "everyone home"},
    {179000, Effect::NeonSweep, 110, "open every door"},
    {188000, Effect::Finale, 132, "house finale"},
    {265000, Effect::FadeOut, 92, "lights out"},
};

constexpr Cue kDancingQueen[] = {
    {0, Effect::MirrorBall, 62, "mirror-ball intro"},
    {29000, Effect::DiscoBlocks, 82, "Friday night"},
    {41000, Effect::BeatPulse, 108, "dancing queen"},
    {55000, Effect::MirrorBall, 94, "seventeen sparkle"},
    {65000, Effect::Wave, 78, "blue dance floor"},
    {88000, Effect::DiscoBlocks, 98, "digging the scene"},
    {134000, Effect::BeatPulse, 118, "queen returns"},
    {159000, Effect::MirrorBall, 108, "tambourine stars"},
    {174000, Effect::DiscoBlocks, 116, "disco runway"},
    {187000, Effect::BeatPulse, 128, "final queen"},
    {199000, Effect::Finale, 138, "royal dance"},
    {223000, Effect::FadeOut, 94, "mirror-ball fade"},
};

constexpr Cue kWhatAFeeling[] = {
    {0, Effect::Glow, 44, "dark studio"},
    {12000, Effect::NeonSweep, 64, "first neon"},
    {28000, Effect::Wave, 72, "slow build"},
    {47000, Effect::BeatPulse, 92, "take your passion"},
    {69000, Effect::NeonSweep, 88, "leg-warmer lasers"},
    {81000, Effect::BeatPulse, 110, "what a feeling"},
    {101000, Effect::Sparkle, 96, "wet-stage glitter"},
    {113000, Effect::NeonSweep, 104, "electric climb"},
    {127000, Effect::BeatPulse, 124, "being's believing"},
    {164000, Effect::Wave, 100, "neon release"},
    {178000, Effect::BeatPulse, 132, "final feeling"},
    {191000, Effect::Finale, 142, "flashdance finale"},
    {232000, Effect::FadeOut, 90, "studio fade"},
};

constexpr Cue kFootloose[] = {
    {0, Effect::KickChase, 72, "shoe-tap intro"},
    {11000, Effect::BootStomp, 86, "first kick"},
    {22000, Effect::Wave, 78, "small-town run"},
    {32000, Effect::BeatPulse, 108, "cut loose"},
    {53000, Effect::KickChase, 100, "running feet"},
    {64000, Effect::BootStomp, 112, "heel-toe riot"},
    {85000, Effect::BeatPulse, 122, "second cut loose"},
    {101000, Effect::KickChase, 116, "dance sprint"},
    {113000, Effect::Wave, 102, "everybody move"},
    {142000, Effect::Finale, 142, "warehouse dance"},
    {163000, Effect::FadeOut, 100, "last kick"},
};

constexpr Cue kManiac[] = {
    {0, Effect::Glow, 42, "cold blue intro"},
    {12000, Effect::LaserChase, 72, "pulse appears"},
    {23000, Effect::NeonSweep, 88, "steel-town runner"},
    {63000, Effect::BeatPulse, 112, "maniac chorus"},
    {72000, Effect::LaserChase, 102, "razor chase"},
    {88000, Effect::Wave, 88, "blue tension"},
    {133000, Effect::BeatPulse, 124, "second maniac"},
    {145000, Effect::NeonSweep, 114, "dance-floor sprint"},
    {173000, Effect::LaserChase, 122, "electric pursuit"},
    {186000, Effect::BeatPulse, 136, "final maniac"},
    {198000, Effect::Finale, 146, "neon overload"},
    {207000, Effect::LaserChase, 128, "last sprint"},
    {228000, Effect::FadeOut, 92, "cold fade"},
};

constexpr Cue kJesusHotrod[] = {
    {0, Effect::IndustrialSparks, 66, "engine priming"},
    {8000, Effect::KickChase, 92, "ignition"},
    {25000, Effect::Firestorm, 108, "hotrod launch"},
    {44000, Effect::IndustrialSparks, 104, "grinder sparks"},
    {73000, Effect::BeatPulse, 124, "engine hammer"},
    {103000, Effect::Firestorm, 126, "redline"},
    {126000, Effect::LaserChase, 116, "machine chase"},
    {145000, Effect::IndustrialSparks, 124, "welding storm"},
    {160000, Effect::Firestorm, 138, "overdrive"},
    {174000, Effect::BeatPulse, 142, "piston chorus"},
    {187000, Effect::KickChase, 132, "hotrod sprint"},
    {206000, Effect::Finale, 152, "industrial meltdown"},
    {276000, Effect::FadeOut, 100, "engine shutdown"},
};

#define SHOW(channelValue, title, cueArray, end, beat, phase, primary, secondary, accent, sat) \
  {channelValue, title, cueArray, static_cast<uint8_t>(sizeof(cueArray) / sizeof(cueArray[0])), \
   end, beat, phase, primary, secondary, accent, sat}

constexpr Show kShows[] = {
    SHOW(1, "Purple Rain", kPurpleRain, 245500, 517, 127, 194, 214, 176, 245),
    SHOW(2, "Africa", kAfrica, 294500, 645, 85, 22, 96, 160, 240),
    SHOW(3, "Whose Bed", kWhoseBed, 265400, 448, 112, 232, 40, 8, 235),
    SHOW(4, "It's My House", kItsMyHouse, 274600, 600, 379, 224, 38, 160, 230),
    SHOW(5, "Dancing Queen", kDancingQueen, 232900, 600, 176, 160, 42, 224, 225),
    SHOW(6, "What a Feeling", kWhatAFeeling, 247900, 488, 259, 132, 226, 180, 225),
    SHOW(7, "Footloose", kFootloose, 175700, 347, 205, 2, 28, 50, 245),
    SHOW(8, "Maniac", kManiac, 237700, 375, 145, 160, 2, 128, 240),
    SHOW(9, "Jesus Built My Hotrod", kJesusHotrod, 291900, 476, 92, 0, 24, 150, 250),
};
constexpr size_t kShowCount = sizeof(kShows) / sizeof(kShows[0]);
#undef SHOW

const Show* showFor(uint8_t channel) {
  for (const auto& show : kShows) {
    if (show.channel == channel) return &show;
  }
  return nullptr;
}

uint8_t hueBlend(uint8_t a, uint8_t b, uint8_t amount) {
  return static_cast<uint8_t>(a + scale8(static_cast<uint8_t>(b - a), amount));
}
}  // namespace

void LightShow::begin() {
  FastLED.addLeds<WS2812B, Config::LED_STRIP_PIN, GRB>(
      leds_, Config::LED_STRIP_COUNT);
  FastLED.setBrightness(Config::LED_STRIP_BRIGHTNESS);
  fill_solid(leds_, Config::LED_STRIP_COUNT, CRGB::Black);
  FastLED.show();
  Serial.printf("[LEDS] data_pin=%u count=%u brightness=%u song_shows=%u\n",
                Config::LED_STRIP_PIN, Config::LED_STRIP_COUNT,
                Config::LED_STRIP_BRIGHTNESS,
                static_cast<unsigned>(kShowCount));
}

void LightShow::handle(const SpeakerAudio& audio, bool sessionActive) {
  if (!sessionActive) {
    if (sessionLightingActive_) {
      fill_solid(leds_, Config::LED_STRIP_COUNT, CRGB::Black);
      FastLED.show();
      sessionLightingActive_ = false;
      activeShowChannel_ = -1;
      lastCue_ = -1;
      Serial.println("[LEDS] session ended; strip off");
    }
    return;
  }
  if (!sessionLightingActive_) {
    sessionLightingActive_ = true;
    lastFrameMs_ = 0;
    Serial.println("[LEDS] authorized session; strip on");
  }

  const uint32_t now = millis();
  if (now - lastFrameMs_ < Config::LED_ANIMATION_INTERVAL_MS) return;
  lastFrameMs_ = now;
  bass_ = audio.bassLevel();
  mid_ = audio.midLevel();
  treble_ = audio.trebleLevel();
  volume_ = audio.volumeLevel();

  const Show* show = audio.songPlaying() ? showFor(audio.channel()) : nullptr;
  if (show != nullptr) {
    if (activeShowChannel_ != static_cast<int8_t>(show->channel)) {
      activeShowChannel_ = static_cast<int8_t>(show->channel);
      lastCue_ = -1;
      Serial.printf("[LEDS] show started channel=%u name=%s\n",
                    show->channel, show->name);
    }
    renderShow(*show, audio.playbackPositionMs());
  } else {
    if (activeShowChannel_ >= 0) {
      Serial.println("[LEDS] song show stopped; default rainbow");
      activeShowChannel_ = -1;
      lastCue_ = -1;
    }
    renderRainbow();
  }
  FastLED.show();
}

void LightShow::renderRainbow() {
  fill_rainbow(leds_, Config::LED_STRIP_COUNT, rainbowOffset_,
               Config::LED_RAINBOW_SPACING);
  rainbowOffset_ += Config::LED_RAINBOW_SPEED;
}

void LightShow::renderShow(const Show& show, uint32_t songMs) {
  uint8_t cueIndex = 0;
  for (uint8_t i = 1; i < show.cueCount; ++i) {
    if (songMs < show.cues[i].startMs) break;
    cueIndex = i;
  }
  if (lastCue_ != static_cast<int8_t>(cueIndex)) {
    lastCue_ = static_cast<int8_t>(cueIndex);
    Serial.printf("[LEDS] channel=%u cue=%s position=%lu.%03lus\n",
                  show.channel, show.cues[cueIndex].name,
                  static_cast<unsigned long>(songMs / 1000),
                  static_cast<unsigned long>(songMs % 1000));
  }
  renderCue(show, show.cues[cueIndex], songMs);
}

uint8_t LightShow::beatPulse(const Show& show, uint32_t songMs) const {
  if (show.channel == 1) {
    const uint16_t index = purpleBeatIndex(songMs);
    const uint32_t beat = purpleBeatMs(index);
    if (songMs < beat) return 0;
    const uint32_t nextBeat = purpleBeatMs(
        min<uint16_t>(index + 1U, PurpleRainTiming::BEAT_COUNT - 1U));
    const uint16_t decayMs = static_cast<uint16_t>(
        min<uint32_t>(230U, max<uint32_t>(120U, (nextBeat - beat) / 2U)));
    const uint32_t age = songMs - beat;
    if (age >= decayMs) return 0;
    return static_cast<uint8_t>(255U - age * 255U / decayMs);
  }
  const uint16_t phase = static_cast<uint16_t>(
      (songMs + show.beatMs - show.beatPhaseMs) % show.beatMs);
  const uint16_t decayMs = min<uint16_t>(230, show.beatMs / 2U);
  if (phase >= decayMs) return 0;
  return static_cast<uint8_t>(255U -
      (static_cast<uint32_t>(phase) * 255U / decayMs));
}

uint16_t LightShow::purpleBeatIndex(uint32_t songMs) {
  const uint16_t target = static_cast<uint16_t>(songMs / 10U);
  uint16_t low = 0;
  uint16_t high = PurpleRainTiming::BEAT_COUNT;
  while (low < high) {
    const uint16_t middle = low + (high - low) / 2U;
    if (PurpleRainTiming::BEATS_10MS[middle] <= target)
      low = middle + 1U;
    else
      high = middle;
  }
  return low == 0 ? 0 : low - 1U;
}

uint32_t LightShow::purpleBeatMs(uint16_t index) {
  index = min<uint16_t>(index, PurpleRainTiming::BEAT_COUNT - 1U);
  return static_cast<uint32_t>(PurpleRainTiming::BEATS_10MS[index]) * 10U;
}

uint8_t LightShow::hash8(uint16_t pixel, uint32_t frame) {
  uint32_t value = static_cast<uint32_t>(pixel) * 0x45D9F3BU;
  value ^= frame * 0x27D4EB2DU;
  value ^= value >> 16;
  value *= 0x7FEB352DU;
  return static_cast<uint8_t>((value ^ (value >> 15)) & 0xFFU);
}

CRGB LightShow::color(uint8_t hue, uint8_t saturation, uint8_t value) {
  return CHSV(hue, saturation, value);
}

void LightShow::renderCue(const Show& show, const Cue& cue, uint32_t songMs) {
  const uint8_t beat = beatPulse(show, songMs);
  const uint8_t slow = static_cast<uint8_t>(songMs / 28U);
  const uint16_t center = Config::LED_STRIP_COUNT / 2U;

  // Adapted from Eric Rollei's M5Stack_Audio_Visualizer rhythm-ripple and
  // beat-chase concepts (MIT): exact PCM beats replace microphone detection,
  // and every generated color is constrained to violet/purple for channel 1.
  auto overlayPurpleRipples = [&]() {
    const uint16_t currentBeat = purpleBeatIndex(songMs);
    for (uint8_t offset = 0; offset < 4; ++offset) {
      if (currentBeat < offset) break;
      const uint16_t beatIndex = currentBeat - offset;
      const uint32_t start = purpleBeatMs(beatIndex);
      if (songMs < start) continue;
      const uint32_t age = songMs - start;
      constexpr uint16_t lifetime = 920;
      if (age >= lifetime) continue;
      const uint8_t progress = static_cast<uint8_t>(age * 255U / lifetime);
      const uint8_t expansion = ease8InOutApprox(progress);
      const uint16_t maxRadius = max<uint16_t>(12U, Config::LED_STRIP_COUNT / 3U);
      const uint16_t radius = static_cast<uint16_t>(
          static_cast<uint32_t>(maxRadius) * expansion / 255U);
      const uint16_t rippleCenter =
          hash8(beatIndex, 0x50555250U) % Config::LED_STRIP_COUNT;
      const uint8_t fade = 255U - progress;
      const uint8_t strength = scale8(
          qadd8(cue.intensity, scale8(bass_, 80)), fade);
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint16_t distance = static_cast<uint16_t>(abs(
            static_cast<int>(i) - static_cast<int>(rippleCenter)));
        const uint16_t edge = distance > radius ? distance - radius : radius - distance;
        if (edge > 4U) continue;
        const uint8_t ring = static_cast<uint8_t>(255U - edge * 52U);
        const uint8_t hue = static_cast<uint8_t>(188U + (beatIndex % 23U));
        leds_[i] += color(hue, edge < 2U ? 145 : 235, scale8(strength, ring));
      }
    }
  };

  auto overlayPurpleChasers = [&]() {
    const uint16_t currentBeat = purpleBeatIndex(songMs);
    for (uint8_t offset = 0; offset < 4; ++offset) {
      if (currentBeat < offset) break;
      const uint16_t beatIndex = currentBeat - offset;
      const uint32_t start = purpleBeatMs(beatIndex);
      const uint32_t end = purpleBeatMs(
          min<uint16_t>(beatIndex + 2U, PurpleRainTiming::BEAT_COUNT - 1U));
      if (songMs < start || end <= start || songMs >= end) continue;
      const uint8_t progress = static_cast<uint8_t>(
          (songMs - start) * 255U / (end - start));
      const bool forward = (beatIndex & 1U) == 0;
      const uint16_t head = static_cast<uint16_t>(
          static_cast<uint32_t>(forward ? progress : 255U - progress) *
          (Config::LED_STRIP_COUNT - 1U) / 255U);
      const uint8_t tailLength = static_cast<uint8_t>(8U + scale8(mid_, 14U));
      for (uint8_t tail = 0; tail < tailLength; ++tail) {
        const int index = forward ? static_cast<int>(head) - tail
                                  : static_cast<int>(head) + tail;
        if (index < 0 || index >= Config::LED_STRIP_COUNT) continue;
        const uint8_t tailFade = static_cast<uint8_t>(
            255U - static_cast<uint16_t>(tail) * 255U / tailLength);
        const uint8_t sparkle = sin8(static_cast<uint8_t>(
            songMs / 12U + index * 9U));
        leds_[index] += color(static_cast<uint8_t>(190U + tail * 2U),
                              170U + scale8(tailFade, 70U),
                              scale8(qadd8(cue.intensity, scale8(treble_, 70U)),
                                     scale8(tailFade, qadd8(150U, sparkle / 3U))));
      }
    }
  };

  switch (cue.effect) {
    case Effect::Glow: {
      const uint8_t breath = sin8(static_cast<uint8_t>(songMs / 22U));
      const uint8_t value = cue.intensity / 2U + scale8(breath, cue.intensity / 2U);
      fill_solid(leds_, Config::LED_STRIP_COUNT,
                 color(show.primaryHue, show.saturation, value));
      break;
    }
    case Effect::Wave:
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint8_t wave = sin8(static_cast<uint8_t>(i * 5U - slow));
        leds_[i] = color(hueBlend(show.primaryHue, show.secondaryHue, wave),
                         show.saturation,
                         cue.intensity / 3U + scale8(wave, cue.intensity * 2U / 3U));
      }
      break;
    case Effect::FrequencyWaves:
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint8_t position = static_cast<uint8_t>(
            static_cast<uint32_t>(i) * 255U / Config::LED_STRIP_COUNT);
        const uint8_t bassWave = sin8(static_cast<uint8_t>(position + songMs / 10U));
        const uint8_t midWave = sin8(static_cast<uint8_t>(position * 2U - songMs / 7U));
        const uint8_t trebleWave = sin8(static_cast<uint8_t>(position * 4U + songMs / 4U));
        const uint8_t energy = qadd8(
            scale8(bassWave, bass_ / 2U),
            qadd8(scale8(midWave, mid_ / 3U), scale8(trebleWave, treble_ / 4U)));
        leds_[i] = color(static_cast<uint8_t>(186U + scale8(midWave, 30U)),
                         220U + scale8(bass_, 30U),
                         qadd8(cue.intensity / 4U, scale8(energy, cue.intensity)));
      }
      break;
    case Effect::BeatBreathing: {
      const uint16_t beatIndex = purpleBeatIndex(songMs);
      const uint32_t beatStart = purpleBeatMs(beatIndex);
      const uint32_t beatEnd = purpleBeatMs(
          min<uint16_t>(beatIndex + 1U, PurpleRainTiming::BEAT_COUNT - 1U));
      const uint8_t fraction = beatEnd > beatStart && songMs >= beatStart
          ? static_cast<uint8_t>((songMs - beatStart) * 255U / (beatEnd - beatStart))
          : 0;
      const uint8_t phase = static_cast<uint8_t>((beatIndex % 8U) * 32U + fraction / 8U);
      const uint8_t breath = sin8(static_cast<uint8_t>(phase - 64U));
      const uint8_t value = qadd8(cue.intensity / 5U,
                                  scale8(breath, qadd8(cue.intensity / 2U, volume_ / 4U)));
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint8_t arch = sin8(static_cast<uint8_t>(
            static_cast<uint32_t>(i) * 128U / Config::LED_STRIP_COUNT));
        leds_[i] = color(static_cast<uint8_t>(190U + scale8(arch, 20U)),
                         220, qadd8(value, scale8(arch, cue.intensity / 4U)));
      }
      break;
    }
    case Effect::RhythmRipples:
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint8_t ambient = sin8(static_cast<uint8_t>(i * 4U + songMs / 18U));
        leds_[i] = color(static_cast<uint8_t>(190U + scale8(ambient, 20U)),
                         245, cue.intensity / 10U + scale8(ambient, volume_ / 8U));
      }
      overlayPurpleRipples();
      break;
    case Effect::BeatChase:
      fill_solid(leds_, Config::LED_STRIP_COUNT, color(194, 255, cue.intensity / 12U));
      overlayPurpleChasers();
      break;
    case Effect::SpectrumBands: {
      const uint16_t section = Config::LED_STRIP_COUNT / 3U;
      const uint8_t levels[3] = {bass_, mid_, treble_};
      const uint8_t hues[3] = {188, 200, 218};
      for (uint8_t band = 0; band < 3; ++band) {
        const uint16_t lit = static_cast<uint16_t>(
            static_cast<uint32_t>(levels[band]) * section / 255U);
        for (uint16_t i = 0; i < section; ++i) {
          const uint16_t index = band * section + i;
          const uint8_t flicker = sin8(static_cast<uint8_t>(songMs / (10U - band * 2U) + i * 7U));
          leds_[index] = color(static_cast<uint8_t>(hues[band] + scale8(flicker, 10U)),
                               i < lit ? 220 : 250,
                               i < lit ? qadd8(cue.intensity / 2U, scale8(flicker, cue.intensity / 2U))
                                       : cue.intensity / 12U);
        }
      }
      break;
    }
    case Effect::PurpleStorm:
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint8_t drop = sin8(static_cast<uint8_t>(i * 13U - songMs / 3U));
        const uint8_t sharp = scale8(drop, drop);
        const uint8_t audioLift = qadd8(scale8(bass_, 50U), scale8(treble_, 36U));
        leds_[i] = color(static_cast<uint8_t>(184U + (i % 31U)), 250,
                         cue.intensity / 10U + scale8(sharp, qadd8(cue.intensity, audioLift)));
      }
      overlayPurpleRipples();
      break;
    case Effect::BeatPulse:
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint8_t wave = sin8(static_cast<uint8_t>(i * 3U + songMs / 7U));
        leds_[i] = color(hueBlend(show.primaryHue, show.secondaryHue, wave),
                         beat > 190 ? show.saturation - 60U : show.saturation,
                         qadd8(cue.intensity / 2U + scale8(beat, cue.intensity / 2U),
                               scale8(wave, cue.intensity / 3U)));
      }
      break;
    case Effect::Rain:
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint8_t drop = sin8(static_cast<uint8_t>(i * 11U - songMs / 4U));
        const uint8_t sharp = scale8(drop, drop);
        leds_[i] = color(hueBlend(show.primaryHue, show.accentHue, i % 31U),
                         show.saturation,
                         cue.intensity / 6U + scale8(sharp, cue.intensity));
      }
      break;
    case Effect::Sparkle:
    case Effect::MirrorBall: {
      const uint32_t frame = songMs / (cue.effect == Effect::MirrorBall ? 90U : 70U);
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint8_t wave = sin8(static_cast<uint8_t>(i * 4U + songMs / 9U));
        uint8_t value = cue.intensity / 3U + scale8(wave, cue.intensity / 2U);
        uint8_t sat = show.saturation;
        if (hash8(i, frame) > (cue.effect == Effect::MirrorBall ? 238 : 246)) {
          value = qadd8(cue.intensity, beat / 2U);
          sat = 80;
        }
        leds_[i] = color(hueBlend(show.primaryHue, show.secondaryHue, wave), sat, value);
      }
      break;
    }
    case Effect::Sunrise:
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint8_t position = static_cast<uint8_t>(
            static_cast<uint32_t>(i) * 255U / Config::LED_STRIP_COUNT);
        const uint8_t shimmer = sin8(position + slow);
        leds_[i] = color(hueBlend(show.primaryHue, show.secondaryHue, position),
                         show.saturation,
                         cue.intensity / 2U + scale8(shimmer, cue.intensity / 2U));
      }
      break;
    case Effect::DrumRipple:
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint16_t distance = i < center ? center - i : i - center;
        const uint8_t ring = sin8(static_cast<uint8_t>(distance * 10U - songMs / 3U));
        leds_[i] = color(hueBlend(show.primaryHue, show.accentHue, ring),
                         show.saturation,
                         cue.intensity / 4U + scale8(scale8(ring, ring), qadd8(beat, 60)));
      }
      break;
    case Effect::BootStomp: {
      const bool left = ((songMs + show.beatMs - show.beatPhaseMs) / show.beatMs) & 1U;
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const bool active = (i < center) == left;
        leds_[i] = color(active ? show.primaryHue : show.secondaryHue,
                         show.saturation,
                         active ? qadd8(cue.intensity / 2U, scale8(beat, cue.intensity))
                                : cue.intensity / 4U);
      }
      break;
    }
    case Effect::DiscoBlocks:
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint8_t block = static_cast<uint8_t>(i / 6U + songMs / show.beatMs);
        const uint8_t hue = (block % 3U == 0) ? show.primaryHue :
                            (block % 3U == 1) ? show.secondaryHue : show.accentHue;
        leds_[i] = color(hue, show.saturation,
                         cue.intensity / 2U + scale8(beat, cue.intensity / 2U));
      }
      break;
    case Effect::NeonSweep:
    case Effect::LaserChase:
    case Effect::KickChase: {
      const uint8_t width = cue.effect == Effect::LaserChase ? 5 : 11;
      const uint16_t head = static_cast<uint16_t>(
          (static_cast<uint64_t>(songMs) * (cue.effect == Effect::KickChase ? 2U : 1U) / 7U) %
          Config::LED_STRIP_COUNT);
      fill_solid(leds_, Config::LED_STRIP_COUNT,
                 color(show.primaryHue, show.saturation, cue.intensity / 8U));
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint16_t forward = (i + Config::LED_STRIP_COUNT - head) % Config::LED_STRIP_COUNT;
        if (forward < width) {
          const uint8_t amount = static_cast<uint8_t>(255U - forward * 255U / width);
          leds_[i] = color(hueBlend(show.secondaryHue, show.accentHue, amount),
                           cue.effect == Effect::LaserChase ? 150 : show.saturation,
                           qadd8(cue.intensity / 2U, scale8(amount, cue.intensity)));
        }
      }
      break;
    }
    case Effect::IndustrialSparks: {
      const uint32_t frame = songMs / 55U;
      fill_solid(leds_, Config::LED_STRIP_COUNT,
                 color(show.primaryHue, 255, cue.intensity / 10U));
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint8_t spark = hash8(i / 2U, frame);
        if (spark > 238) {
          leds_[i] = color(spark > 250 ? show.accentHue : show.secondaryHue,
                           spark > 250 ? 90 : 255,
                           qadd8(cue.intensity, beat / 3U));
        }
      }
      break;
    }
    case Effect::Firestorm:
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint8_t turbulence = hash8(i, songMs / 85U);
        const uint8_t flame = sin8(static_cast<uint8_t>(i * 7U - songMs / 3U));
        leds_[i] = color(hueBlend(show.primaryHue, show.secondaryHue, turbulence),
                         255, cue.intensity / 3U + scale8(flame, cue.intensity * 2U / 3U));
      }
      break;
    case Effect::Finale:
      for (uint16_t i = 0; i < Config::LED_STRIP_COUNT; ++i) {
        const uint8_t outward = sin8(static_cast<uint8_t>(i * 9U + songMs / 3U));
        const uint8_t crossing = sin8(static_cast<uint8_t>(i * 5U - songMs / 5U));
        leds_[i] = color(hueBlend(show.primaryHue, show.accentHue, crossing),
                         beat > 190 ? 130 : show.saturation,
                         qadd8(cue.intensity / 3U,
                               qadd8(scale8(outward, cue.intensity / 2U),
                                     scale8(beat, crossing / 2U))));
      }
      if (show.channel == 1) {
        overlayPurpleRipples();
        overlayPurpleChasers();
      }
      break;
    case Effect::FadeOut: {
      const uint32_t fadeLength = show.endMs > cue.startMs ? show.endMs - cue.startMs : 1;
      const uint32_t remaining = songMs >= show.endMs ? 0 : show.endMs - songMs;
      const uint8_t fade = static_cast<uint8_t>(
          remaining >= fadeLength ? 255U : remaining * 255U / fadeLength);
      const uint8_t breath = sin8(static_cast<uint8_t>(songMs / 24U));
      fill_solid(leds_, Config::LED_STRIP_COUNT,
                 color(show.primaryHue, show.saturation,
                       scale8(cue.intensity / 2U + scale8(breath, cue.intensity / 2U), fade)));
      break;
    }
  }
}
