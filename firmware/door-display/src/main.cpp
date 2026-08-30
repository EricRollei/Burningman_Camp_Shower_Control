#include <Arduino.h>
#include <M5UnitOLED.h>
#include <WiFi.h>

#include "CampNetEspNow.h"
#include "CampNetProtocol.h"

// Which shower this sign belongs to. Set per PlatformIO environment
// (-DDOOR_STATION_ID=1 for door1, 2 for door2) and must match the Tough's
// STATION_ID. The sign ignores every other station on the air.
#ifndef DOOR_STATION_ID
#define DOOR_STATION_ID 1
#endif

namespace {

constexpr uint8_t kOledSdaPin = 2;
constexpr uint8_t kOledSclPin = 1;
constexpr uint8_t kButtonPin = 9;
constexpr uint32_t kI2cFrequencyHz = 100000;
constexpr uint32_t kMessageIntervalMs = 3000;
constexpr uint32_t kDebounceMs = 40;
constexpr uint32_t kStatusTimeoutMs = 5000;  // STATUS shares the Tough tx FIFO with member/usage bursts
constexpr uint32_t kRadioRetryIntervalMs = 5000;
constexpr uint8_t kStationId = DOOR_STATION_ID;
static_assert(kStationId >= 1 && kStationId <= CampNet::MAX_STATIONS, "DOOR_STATION_ID out of range");

M5UnitOLED display(kOledSdaPin, kOledSclPin, kI2cFrequencyHz);

enum class DoorState { Offline, Open, InUse, Unavailable };

const char* const kOpenMessages[] = {
    "HEY STINKY",
    "SHOWER TIME",
    "SCRUB A DUB",
    "GET FRESH",
    "SOAP AWAITS",
};

constexpr size_t kOpenMessageCount =
    sizeof(kOpenMessages) / sizeof(kOpenMessages[0]);

bool lastButtonReading = HIGH;
bool buttonState = HIGH;
bool radioReady = false;
bool receivedStatus = false;
uint32_t lastButtonChangeMs = 0;
uint32_t lastMessageChangeMs = 0;
uint32_t lastStatusReceivedMs = 0;
uint32_t lastRadioAttemptMs = 0;
uint32_t packetsSeen = 0;
uint32_t packetsForUs = 0;
size_t messageIndex = 0;
DoorState doorState = DoorState::Offline;

// The ESP-NOW receive callback runs on the Wi-Fi task. It only parks the
// latest matching status here; the loop consumes it.
volatile bool pendingStatus = false;
volatile uint8_t pendingDoorState = CampNet::DOOR_UNAVAILABLE;

void drawCenteredText(const char* text, int32_t y, uint8_t size,
                      uint16_t foreground, uint16_t background) {
  display.setTextDatum(top_center);
  display.setTextColor(foreground, background);
  display.setTextSize(size);
  display.drawString(text, display.width() / 2, y);
}

// Tiny station badge so a sign flashed for the wrong shower is obvious.
void drawBadge(uint16_t foreground, uint16_t background) {
  char badge[4];
  snprintf(badge, sizeof(badge), "S%u", kStationId);
  display.setTextDatum(top_right);
  display.setTextColor(foreground, background);
  display.setTextSize(1);
  display.drawString(badge, display.width() - 1, 0);
}

void drawOpenScreen() {
  display.startWrite();
  display.fillScreen(TFT_WHITE);
  drawCenteredText("OPEN", 3, 2, TFT_BLACK, TFT_WHITE);

  const char* message = kOpenMessages[messageIndex];
  display.setTextSize(2);
  const uint8_t messageSize = display.textWidth(message) <= display.width() - 4 ? 2 : 1;
  const int32_t messageY = messageSize == 2 ? 34 : 39;
  drawCenteredText(message, messageY, messageSize, TFT_BLACK, TFT_WHITE);
  drawBadge(TFT_BLACK, TFT_WHITE);
  display.endWrite();

  Serial.printf("Display: OPEN / %s\n", message);
}

void drawInUseScreen() {
  display.startWrite();
  display.fillScreen(TFT_BLACK);
  drawCenteredText("IN USE", 20, 3, TFT_WHITE, TFT_BLACK);
  drawBadge(TFT_WHITE, TFT_BLACK);
  display.endWrite();

  Serial.println("Display: IN USE");
}

void drawUnavailableScreen() {
  display.startWrite();
  display.fillScreen(TFT_BLACK);
  drawCenteredText("UNAVAILABLE", 16, 1, TFT_WHITE, TFT_BLACK);
  drawCenteredText("TRY LATER", 36, 2, TFT_WHITE, TFT_BLACK);
  drawBadge(TFT_WHITE, TFT_BLACK);
  display.endWrite();

  Serial.println("Display: UNAVAILABLE");
}

void drawOfflineScreen() {
  display.startWrite();
  display.fillScreen(TFT_BLACK);
  drawCenteredText(receivedStatus ? "OFFLINE" : "LISTENING", 14, 2,
                   TFT_WHITE, TFT_BLACK);
  char line[24];
  snprintf(line, sizeof(line), "FOR SHOWER %u", kStationId);
  drawCenteredText(line, 42, 1, TFT_WHITE, TFT_BLACK);
  drawBadge(TFT_WHITE, TFT_BLACK);
  display.endWrite();

  Serial.printf("Display: %s\n", receivedStatus ? "OFFLINE" : "LISTENING");
}

void drawCurrentScreen() {
  switch (doorState) {
    case DoorState::Open: drawOpenScreen(); break;
    case DoorState::InUse: drawInUseScreen(); break;
    case DoorState::Unavailable: drawUnavailableScreen(); break;
    case DoorState::Offline: drawOfflineScreen(); break;
  }
}

void setDoorState(DoorState state) {
  if (state == doorState) return;
  doorState = state;
  if (state == DoorState::Open) lastMessageChangeMs = millis();
  drawCurrentScreen();
}

void updateButton() {
  const bool reading = digitalRead(kButtonPin);
  const uint32_t now = millis();

  if (reading != lastButtonReading) {
    lastButtonChangeMs = now;
    lastButtonReading = reading;
  }

  if ((now - lastButtonChangeMs) >= kDebounceMs && reading != buttonState) {
    buttonState = reading;
    if (buttonState == LOW) {
      // The Tough is authoritative. The button only redraws the current
      // state; it never overrides occupancy.
      drawCurrentScreen();
      Serial.printf("NanoC6 button: redraw (packets seen=%lu, for us=%lu)\n",
                    static_cast<unsigned long>(packetsSeen),
                    static_cast<unsigned long>(packetsForUs));
    }
  }
}

void updateCarousel() {
  if (doorState != DoorState::Open) {
    return;
  }

  const uint32_t now = millis();
  if ((now - lastMessageChangeMs) < kMessageIntervalMs) {
    return;
  }

  lastMessageChangeMs = now;
  messageIndex = (messageIndex + 1) % kOpenMessageCount;
  drawOpenScreen();
}

void onReceive(CAMPNET_RECV_CB_PARAMS) {
  ++packetsSeen;
  if (!CampNet::headerValid(data, length)) return;
  CampNet::Header header;
  memcpy(&header, data, sizeof(header));
  if (header.type != CampNet::PKT_STATUS || header.stationId != kStationId ||
      header.role != CampNet::ROLE_SHOWER) return;
  if (length < static_cast<int>(sizeof(CampNet::StatusPacket))) return;
  CampNet::StatusPacket packet;
  memcpy(&packet, data, sizeof(packet));
  ++packetsForUs;
  pendingDoorState = packet.doorState;
  pendingStatus = true;
}

bool beginRadio() {
  lastRadioAttemptMs = millis();
  // Never-connected STA parked on the shared channel. No WiFi.begin(): a
  // connecting station would hop channels and stop hearing the Tough.
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(CampNet::CHANNEL);
  const uint32_t startedMs = millis();
  while (!WiFi.STA.started() && millis() - startedMs < 1000) delay(10);
  if (!WiFi.STA.started()) {
    Serial.println("Radio: STA interface did not start");
    return false;
  }
  if (esp_now_init() != ESP_OK) {
    Serial.println("Radio: esp_now_init failed");
    return false;
  }
  esp_now_register_recv_cb(onReceive);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, CampNet::BROADCAST_MAC, sizeof(peer.peer_addr));
  peer.ifidx = WIFI_IF_STA;
  peer.channel = 0;
  peer.encrypt = false;
  if (!esp_now_is_peer_exist(CampNet::BROADCAST_MAC) && esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Radio: could not add broadcast peer");
    esp_now_deinit();
    return false;
  }
  Serial.printf("Radio: ESP-NOW listening on channel %u for shower %u (mac %s)\n",
                CampNet::CHANNEL, kStationId, WiFi.macAddress().c_str());
  return true;
}

void serviceRadio() {
  if (radioReady) return;
  if (millis() - lastRadioAttemptMs >= kRadioRetryIntervalMs) radioReady = beginRadio();
}

void receiveStatus() {
  if (!pendingStatus) return;
  pendingStatus = false;
  const uint8_t state = pendingDoorState;

  DoorState nextState;
  if (state == CampNet::DOOR_OPEN) nextState = DoorState::Open;
  else if (state == CampNet::DOOR_IN_USE) nextState = DoorState::InUse;
  else if (state == CampNet::DOOR_UNAVAILABLE) nextState = DoorState::Unavailable;
  else return;

  lastStatusReceivedMs = millis();
  receivedStatus = true;
  setDoorState(nextState);
}

void enforceStatusTimeout() {
  if ((!receivedStatus || millis() - lastStatusReceivedMs > kStatusTimeoutMs) &&
      doorState != DoorState::Offline) {
    setDoorState(DoorState::Offline);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("M5NanoC6 door display starting for shower %u\n", kStationId);
  Serial.printf("SH1107: SDA GPIO%u, SCL GPIO%u, address 0x3C\n",
                kOledSdaPin, kOledSclPin);

  pinMode(kButtonPin, INPUT_PULLUP);

  if (!display.begin()) {
    Serial.println("ERROR: SH1107 display was not found; rebooting in 5 s");
    delay(5000);
    ESP.restart();
  }

  if (display.width() < display.height()) {
    display.setRotation(display.getRotation() ^ 1);
  }

  Serial.printf("SH1107 ready: %d x %d\n", display.width(), display.height());
  drawOfflineScreen();
  radioReady = beginRadio();
  Serial.println("The Tough controls OPEN / IN USE over CampNet (ESP-NOW)");
}

void loop() {
  serviceRadio();
  receiveStatus();
  enforceStatusTimeout();
  updateButton();
  updateCarousel();
  delay(5);
}
