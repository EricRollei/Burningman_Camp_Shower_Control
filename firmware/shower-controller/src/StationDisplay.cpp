#include "StationDisplay.h"

#include <M5Unified.h>

#include "BigTopDisplayFont.h"
#include "Config.h"

namespace {
constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint16_t>(((red & 0xF8U) << 8U) |
                               ((green & 0xFCU) << 3U) | (blue >> 3U));
}

constexpr uint16_t BIG_TOP_RED = rgb565(142, 27, 34);
constexpr uint16_t BIG_TOP_DARK = rgb565(58, 10, 13);
constexpr uint16_t BIG_TOP_CREAM = rgb565(249, 235, 208);
constexpr uint16_t BIG_TOP_GOLD = rgb565(242, 193, 78);
constexpr uint16_t BIG_TOP_ORANGE = rgb565(227, 154, 43);
constexpr uint16_t BIG_TOP_DENIED = rgb565(255, 179, 167);
constexpr uint16_t START_GREEN = rgb565(25, 138, 67);
constexpr uint16_t STOP_RED = rgb565(202, 38, 45);
constexpr int32_t FOOTER_X = 22;
constexpr int32_t FOOTER_Y = 198;
constexpr int32_t FOOTER_W = 276;
constexpr int32_t FOOTER_H = 27;
constexpr int32_t CONTROL_X = 30;
constexpr int32_t CONTROL_Y = 140;
constexpr int32_t CONTROL_W = 260;
constexpr int32_t CONTROL_H = 50;

bool isAsciiUpperDisplayable(const String& value) {
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t ch = static_cast<uint8_t>(value[i]);
    if (ch < 0x20U || ch > 0x5AU) return false;
  }
  return true;
}
}  // namespace

void StationDisplay::begin() {
  M5.Display.setTextWrap(false);
  invalidate();
}

void StationDisplay::invalidate() {
  layout_ = Layout::NONE;
  lastActiveSecond_ = UINT32_MAX;
  lastPeers_ = UINT8_MAX;
  statusValid_ = false;
}

String StationDisplay::displayName(const char* name) const {
  String value = name == nullptr ? "" : String(name);
  value.trim();

  // Mad T's loaner wristbands are registered by number. Keep the compact
  // numeric registry value for administration, but make the camp identity
  // explicit on the person-facing station display.
  bool numeric = !value.isEmpty();
  for (size_t i = 0; i < value.length() && numeric; ++i) {
    numeric = value[i] >= '0' && value[i] <= '9';
  }
  if (numeric) {
    value = "Mad T " + value;
  } else {
    // Named campers see first name + last initial. The registry, dashboard,
    // telemetry and logs retain the full name; this only formats the Tough UI.
    const int firstSpace = value.indexOf(' ');
    if (firstSpace > 0) {
      int lastEnd = static_cast<int>(value.length()) - 1;
      while (lastEnd >= 0 && value[lastEnd] == ' ') --lastEnd;
      int lastStart = lastEnd;
      while (lastStart > 0 && value[lastStart - 1] != ' ') --lastStart;
      if (lastStart > firstSpace && lastStart <= lastEnd) {
        value = value.substring(0, firstSpace) + " " + value[lastStart] + ".";
      }
    }
  }
  value.toUpperCase();
  if (value.length() > 24U) {
    value.remove(21);
    value += "...";
  }
  return value;
}

void StationDisplay::drawBigTopText(const char* text, int32_t x, int32_t y,
                                    float scale, uint16_t color,
                                    lgfx::textdatum::textdatum_t datum) {
  M5.Display.setFont(&BigTopDisplay18pt7b);
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(scale);
  M5.Display.setTextColor(BIG_TOP_DARK);
  M5.Display.drawString(text, x + 2, y + 2);
  M5.Display.setTextColor(color);
  M5.Display.drawString(text, x, y);
}

void StationDisplay::drawPlainCentered(const String& text, int32_t y,
                                        const lgfx::IFont* font, float scale,
                                        uint16_t color) {
  M5.Display.setFont(font);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(scale);
  M5.Display.setTextColor(color, BIG_TOP_RED);
  M5.Display.drawString(text, 160, y);
}

void StationDisplay::drawFrame() {
  M5.Display.fillScreen(BIG_TOP_RED);

  // Alternating wedges converge above the display, matching the mockup's
  // cream/red circus canopy without maintaining a framebuffer.
  constexpr int32_t apexX = 160;
  constexpr int32_t apexY = -62;
  for (int32_t x = -160, wedge = 0; x < 480; x += 46, ++wedge) {
    if ((wedge & 1) == 0) {
      M5.Display.fillTriangle(apexX, apexY, x, 68, x + 23, 68, BIG_TOP_CREAM);
    }
  }

  M5.Display.fillRoundRect(8, 22, 304, 211, 12, BIG_TOP_RED);
  M5.Display.drawRoundRect(8, 22, 304, 211, 12, BIG_TOP_GOLD);
  M5.Display.drawRoundRect(9, 23, 302, 209, 11, BIG_TOP_GOLD);
  M5.Display.drawRoundRect(13, 27, 294, 201, 9, BIG_TOP_GOLD);

  for (int32_t x = 20; x <= 300; x += 16) {
    M5.Display.fillCircle(x, 27, 3, BIG_TOP_ORANGE);
    M5.Display.fillCircle(x, 27, 1, BIG_TOP_CREAM);
    M5.Display.fillCircle(x, 228, 3, BIG_TOP_ORANGE);
    M5.Display.fillCircle(x, 228, 1, BIG_TOP_CREAM);
  }
  for (int32_t y = 43; y <= 219; y += 16) {
    M5.Display.fillCircle(12, y, 3, BIG_TOP_ORANGE);
    M5.Display.fillCircle(12, y, 1, BIG_TOP_CREAM);
    M5.Display.fillCircle(307, y, 3, BIG_TOP_ORANGE);
    M5.Display.fillCircle(307, y, 1, BIG_TOP_CREAM);
  }
}

void StationDisplay::drawTopStatus(uint8_t role, bool ready, uint8_t peers) {
  M5.Display.fillRect(16, 30, 288, 14, BIG_TOP_RED);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(BIG_TOP_GOLD, BIG_TOP_RED);
  M5.Display.drawString(Config::ROLE_HEADER_LABELS[role], 20, 37);
  char status[32];
  snprintf(status, sizeof(status), "%s - %u NET", ready ? "READY" : "SERVICE",
           static_cast<unsigned>(peers));
  M5.Display.setTextDatum(middle_right);
  M5.Display.setTextColor(ready ? BIG_TOP_CREAM : BIG_TOP_DENIED, BIG_TOP_RED);
  M5.Display.drawString(status, 300, 37);
  lastReady_ = ready;
  lastPeers_ = peers;
  statusValid_ = true;
}

void StationDisplay::drawFooter(const char* text, bool pale) {
  const uint16_t fill = pale ? BIG_TOP_CREAM : BIG_TOP_GOLD;
  M5.Display.fillRoundRect(FOOTER_X, FOOTER_Y, FOOTER_W, FOOTER_H, 6, fill);
  M5.Display.drawRoundRect(FOOTER_X, FOOTER_Y, FOOTER_W, FOOTER_H, 6,
                           BIG_TOP_DARK);
  M5.Display.drawFastHLine(FOOTER_X + 5, FOOTER_Y + FOOTER_H,
                           FOOTER_W - 10, BIG_TOP_DARK);

  M5.Display.setFont(&BigTopDisplay18pt7b);
  M5.Display.setTextSize(0.39F);
  while (M5.Display.textWidth(text) > FOOTER_W - 12 &&
         M5.Display.getTextSizeX() > 0.28F) {
    M5.Display.setTextSize(M5.Display.getTextSizeX() - 0.02F);
  }
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(BIG_TOP_DARK, fill);
  M5.Display.drawString(text, 160, FOOTER_Y + FOOTER_H / 2 - 1);
}

void StationDisplay::drawControlButton(bool pumpOn) {
  const uint16_t fill = pumpOn ? STOP_RED : START_GREEN;
  M5.Display.fillRoundRect(CONTROL_X + 2, CONTROL_Y + 3, CONTROL_W,
                           CONTROL_H, 10, BIG_TOP_DARK);
  M5.Display.fillRoundRect(CONTROL_X, CONTROL_Y, CONTROL_W, CONTROL_H, 10,
                           fill);
  M5.Display.drawRoundRect(CONTROL_X, CONTROL_Y, CONTROL_W, CONTROL_H, 10,
                           BIG_TOP_CREAM);
  M5.Display.drawRoundRect(CONTROL_X + 1, CONTROL_Y + 1, CONTROL_W - 2,
                           CONTROL_H - 2, 9, BIG_TOP_GOLD);
  drawBigTopText(pumpOn ? "STOP WATER" : "START WATER", 160,
                 CONTROL_Y + CONTROL_H / 2 - 1, 0.68F, BIG_TOP_CREAM);
}

bool StationDisplay::controlButtonContains(int16_t x, int16_t y) const {
  return x >= CONTROL_X && x < CONTROL_X + CONTROL_W && y >= CONTROL_Y &&
         y < CONTROL_Y + CONTROL_H;
}

void StationDisplay::drawFittedName(const char* name, int32_t y,
                                    float maxScale) {
  const String value = displayName(name);
  if (!isAsciiUpperDisplayable(value)) {
    M5.Display.setFont(&fonts::FreeSansBold18pt7b);
    float scale = 1.0F;
    M5.Display.setTextSize(scale);
    while (M5.Display.textWidth(value) > 276 && scale > 0.56F) {
      scale -= 0.04F;
      M5.Display.setTextSize(scale);
    }
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextColor(BIG_TOP_CREAM, BIG_TOP_RED);
    M5.Display.drawString(value, 160, y);
    return;
  }

  M5.Display.setFont(&BigTopDisplay18pt7b);
  float scale = maxScale;
  M5.Display.setTextSize(scale);
  while (M5.Display.textWidth(value) > 276 && scale > 0.48F) {
    scale -= 0.04F;
    M5.Display.setTextSize(scale);
  }
  drawBigTopText(value.c_str(), 160, y, scale, BIG_TOP_CREAM);
}

void StationDisplay::drawGallons(float gallons, int32_t centerY,
                                 uint8_t decimals, uint16_t numberColor,
                                 uint16_t background) {
  char amount[20];
  snprintf(amount, sizeof(amount), "%.*f", decimals, gallons);
  M5.Display.setFont(&fonts::Font8);
  M5.Display.setTextSize(0.84F);
  while (M5.Display.textWidth(amount) > 226 &&
         M5.Display.getTextSizeX() > 0.58F) {
    M5.Display.setTextSize(M5.Display.getTextSizeX() - 0.04F);
  }
  const int32_t numberWidth = M5.Display.textWidth(amount);
  M5.Display.setFont(&BigTopDisplay18pt7b);
  M5.Display.setTextSize(0.48F);
  const int32_t unitWidth = M5.Display.textWidth("GAL");
  const int32_t totalWidth = numberWidth + 8 + unitWidth;
  const int32_t startX = 160 - totalWidth / 2;

  M5.Display.setFont(&fonts::Font8);
  M5.Display.setTextSize(0.84F);
  while (M5.Display.textWidth(amount) > 226 &&
         M5.Display.getTextSizeX() > 0.58F) {
    M5.Display.setTextSize(M5.Display.getTextSizeX() - 0.04F);
  }
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(numberColor, background);
  M5.Display.drawString(amount, startX, centerY);
  drawBigTopText("GAL", startX + numberWidth + 8, centerY + 12, 0.48F,
                 numberColor, middle_left);
}

void StationDisplay::drawIdle(uint8_t role, bool ready, uint8_t peers) {
  M5.Display.startWrite();
  drawFrame();
  drawTopStatus(role, ready, peers);
  drawBigTopText("TAP YOUR", 160, 68, 0.82F, BIG_TOP_CREAM);
  drawBigTopText("WRISTBAND", 160, 99, 0.82F, BIG_TOP_CREAM);
  drawPlainCentered(Config::ROLE_IDLE_PROMPTS[role], 151,
                    &fonts::FreeSansBold12pt7b, 0.72F, BIG_TOP_CREAM);
  drawFooter(Config::ROLE_OPEN_LABELS[role]);
  M5.Display.endWrite();
  layout_ = Layout::IDLE;
}

void StationDisplay::drawSession(uint8_t role, const char* name,
                                 float burnTotal, float sessionGallons,
                                 uint32_t elapsedMs, bool pumpOn, bool ready,
                                 uint8_t peers) {
  if (pumpOn && layout_ == Layout::ACTIVE) {
    M5.Display.startWrite();
    drawActiveMetrics(role, name, sessionGallons, elapsedMs, ready, peers,
                      false);
    M5.Display.endWrite();
    return;
  }

  M5.Display.startWrite();
  drawFrame();
  drawTopStatus(role, ready, peers);
  if (!pumpOn) {
    drawPlainCentered("HOWDY", 55, &fonts::FreeSansBold12pt7b, 0.72F,
                      BIG_TOP_CREAM);
    drawFittedName(name, 82, 0.90F);
    char total[64];
    snprintf(total, sizeof(total), "%.1f GALLONS USED THIS BURN", burnTotal);
    drawPlainCentered(total, 121, &fonts::FreeSansBold12pt7b, 0.58F,
                      BIG_TOP_CREAM);
    drawControlButton(false);
    drawFooter("PHYSICAL BUTTON ALSO WORKS");
    layout_ = Layout::LOGIN;
  } else {
    drawActiveMetrics(role, name, sessionGallons, elapsedMs, ready, peers,
                      true);
    drawControlButton(true);
    drawFooter("PHYSICAL BUTTON ALSO WORKS", true);
    layout_ = Layout::ACTIVE;
  }
  M5.Display.endWrite();
}

void StationDisplay::drawActiveMetrics(uint8_t role, const char* name,
                                       float sessionGallons,
                                       uint32_t elapsedMs, bool ready,
                                       uint8_t peers, bool fullRedraw) {
  if (!statusValid_ || ready != lastReady_ || peers != lastPeers_) {
    drawTopStatus(role, ready, peers);
  }

  if (fullRedraw) {
    M5.Display.fillRect(20, 47, 280, 89, BIG_TOP_RED);
  } else {
    // Flow pulses can be frequent. Refresh only the hero number, leaving the
    // frame, bulbs, instruction band and static copy untouched.
    M5.Display.fillRect(20, 75, 280, 61, BIG_TOP_RED);
  }

  const uint32_t seconds = elapsedMs / 1000UL;
  if (fullRedraw || seconds != lastActiveSecond_) {
    M5.Display.fillRect(20, 47, 280, 25, BIG_TOP_RED);
    char heading[64];
    snprintf(heading, sizeof(heading), "%s - %02lu:%02lu",
             displayName(name).c_str(),
             static_cast<unsigned long>(seconds / 60UL),
             static_cast<unsigned long>(seconds % 60UL));
    drawPlainCentered(heading, 58, &fonts::FreeSansBold12pt7b, 0.68F,
                      BIG_TOP_CREAM);
    lastActiveSecond_ = seconds;
  }
  drawGallons(sessionGallons, 96, 2, BIG_TOP_GOLD, BIG_TOP_RED);
  if (fullRedraw) {
    drawPlainCentered(Config::ROLE_USED_LABELS[role], 129,
                      &fonts::FreeSansBold12pt7b, 0.58F, BIG_TOP_CREAM);
  }
}

void StationDisplay::drawSummary(uint8_t role, float sessionGallons,
                                 float burnTotal, uint32_t elapsedMs,
                                 bool logged, bool ready, uint8_t peers) {
  M5.Display.startWrite();
  drawFrame();
  drawTopStatus(role, ready, peers);
  drawBigTopText(logged ? "ALL DONE!" : "LOGGING ERROR", 160, 62,
                 logged ? 0.82F : 0.62F,
                 logged ? BIG_TOP_CREAM : BIG_TOP_DENIED);

  M5.Display.fillRoundRect(30, 88, 260, 100, 8, BIG_TOP_CREAM);
  M5.Display.fillCircle(30, 138, 8, BIG_TOP_RED);
  M5.Display.fillCircle(290, 138, 8, BIG_TOP_RED);
  drawGallons(sessionGallons, 116, 1, BIG_TOP_DARK, BIG_TOP_CREAM);
  char total[48];
  snprintf(total, sizeof(total), "%.1f GAL TOTAL THIS BURN", burnTotal);
  M5.Display.setFont(&BigTopDisplay18pt7b);
  M5.Display.setTextSize(0.38F);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(BIG_TOP_DARK, BIG_TOP_CREAM);
  M5.Display.drawString(total, 160, 157);
  const uint32_t seconds = elapsedMs / 1000UL;
  char elapsed[40];
  snprintf(elapsed, sizeof(elapsed), "%lu MIN %02lu SEC",
           static_cast<unsigned long>(seconds / 60UL),
           static_cast<unsigned long>(seconds % 60UL));
  M5.Display.setFont(&fonts::FreeSansBold9pt7b);
  M5.Display.setTextSize(0.72F);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(BIG_TOP_DARK, BIG_TOP_CREAM);
  M5.Display.drawString(elapsed, 160, 176);
  drawFooter(logged ? Config::ROLE_THANKS_LABELS[role] : "TELL A CAMP ADMIN");
  M5.Display.endWrite();
  layout_ = Layout::SUMMARY;
}

void StationDisplay::drawMessage(uint8_t role, const String& title,
                                 const String& body, bool ready,
                                 uint8_t peers) {
  M5.Display.startWrite();
  drawFrame();
  drawTopStatus(role, ready, peers);
  String heading = title;
  heading.toUpperCase();
  const bool denied = heading == "NOT AUTHORIZED";
  const bool unavailable = heading == "UNAVAILABLE" || heading.indexOf("FAILED") >= 0 ||
                           heading.indexOf("ERROR") >= 0;
  if (heading == "NOT AUTHORIZED") {
    drawBigTopText("NOT", 160, 78, 0.86F, BIG_TOP_DENIED);
    drawBigTopText("AUTHORIZED", 160, 112, 0.76F, BIG_TOP_DENIED);
  } else {
    M5.Display.setFont(&BigTopDisplay18pt7b);
    float scale = 0.76F;
    M5.Display.setTextSize(scale);
    while (M5.Display.textWidth(heading) > 278 && scale > 0.42F) {
      scale -= 0.04F;
      M5.Display.setTextSize(scale);
    }
    drawBigTopText(heading.c_str(), 160, 91, scale,
                   unavailable ? BIG_TOP_DENIED : BIG_TOP_CREAM);
  }
  drawPlainCentered(body, 153, &fonts::FreeSansBold12pt7b, 0.68F,
                    BIG_TOP_CREAM);
  const char* footer = "RETURNING TO READY";
  if (denied) {
    footer = body == "Wristband disabled" ? "WRISTBAND DISABLED"
                                          : "WRISTBAND NOT RECOGNIZED";
  }
  drawFooter(footer);
  M5.Display.endWrite();
  layout_ = Layout::MESSAGE;
}

void StationDisplay::drawCalibration(uint32_t pulses, bool ready,
                                     uint8_t peers) {
  M5.Display.startWrite();
  drawFrame();
  drawTopStatus(Config::STATION_ROLE_VALUE, ready, peers);
  drawBigTopText("DISPENSING", 160, 73, 0.70F, BIG_TOP_GOLD);
  char pulseText[48];
  snprintf(pulseText, sizeof(pulseText), "%lu PULSES",
           static_cast<unsigned long>(pulses));
  drawPlainCentered(pulseText, 122, &fonts::FreeSansBold18pt7b, 0.9F,
                    BIG_TOP_CREAM);
  drawPlainCentered("STOP FROM THE ADMIN PAGE", 164,
                    &fonts::FreeSansBold12pt7b, 0.62F, BIG_TOP_CREAM);
  drawFooter("CALIBRATION - PUMP IS ON", true);
  M5.Display.endWrite();
  layout_ = Layout::CALIBRATION;
}
