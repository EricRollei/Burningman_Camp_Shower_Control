#pragma once

#include <M5Unified.h>

class StationDisplay {
 public:
  void begin();
  void invalidate();

  void drawIdle(uint8_t role, bool ready, uint8_t peers);
  void drawSession(uint8_t role, const char* name, float burnTotal,
                   float sessionGallons, uint32_t elapsedMs, bool pumpOn,
                   bool ready, uint8_t peers);
  void drawMusicChannel(uint8_t role, uint8_t channel, const char* channelName,
                        bool pumpOn, bool ready, uint8_t peers);
  void drawSummary(uint8_t role, float sessionGallons, float burnTotal,
                   uint32_t elapsedMs, bool logged, bool ready, uint8_t peers);
  void drawMessage(uint8_t role, const String& title, const String& body,
                   bool ready, uint8_t peers);
  void drawCalibration(uint32_t pulses, bool ready, uint8_t peers);
  bool controlButtonContains(int16_t x, int16_t y) const;

 private:
  enum class Layout {
    NONE, IDLE, LOGIN, ACTIVE, MUSIC_CHANNEL, SUMMARY, MESSAGE, CALIBRATION
  };

  void drawFrame();
  void drawTopStatus(uint8_t role, bool ready, uint8_t peers);
  void drawFooter(const char* text, bool pale = false);
  void drawControlButton(bool pumpOn);
  void drawBigTopText(const char* text, int32_t x, int32_t y, float scale,
                      uint16_t color,
                      lgfx::textdatum::textdatum_t datum =
                          lgfx::textdatum::middle_center);
  void drawFittedName(const char* name, int32_t y, float maxScale = 1.0F);
  void drawPlainCentered(const String& text, int32_t y, const lgfx::IFont* font,
                         float scale, uint16_t color);
  void drawGallons(float gallons, int32_t centerY, uint8_t decimals,
                   uint16_t numberColor, uint16_t background);
  void drawActiveMetrics(uint8_t role, const char* name, float sessionGallons,
                         uint32_t elapsedMs, bool ready, uint8_t peers,
                         bool fullRedraw);
  String displayName(const char* name) const;

  Layout layout_ = Layout::NONE;
  uint32_t lastActiveSecond_ = UINT32_MAX;
  uint8_t lastPeers_ = UINT8_MAX;
  bool lastReady_ = false;
  bool statusValid_ = false;
};
