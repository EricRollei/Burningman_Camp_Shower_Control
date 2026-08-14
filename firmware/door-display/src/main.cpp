#include <Arduino.h>
#include <M5UnitOLED.h>
#include <WiFi.h>
#include <WiFiUdp.h>

namespace {

constexpr uint8_t kOledSdaPin = 2;
constexpr uint8_t kOledSclPin = 1;
constexpr uint8_t kButtonPin = 9;
constexpr uint32_t kI2cFrequencyHz = 100000;
constexpr uint32_t kMessageIntervalMs = 3000;
constexpr uint32_t kDebounceMs = 40;
constexpr uint32_t kStatusRequestIntervalMs = 500;
constexpr uint32_t kStatusTimeoutMs = 3000;
constexpr uint32_t kWifiRetryIntervalMs = 5000;
constexpr uint16_t kToughStatusPort = 4210;
constexpr uint16_t kLocalStatusPort = 4211;
constexpr char kWifiName[] = "CampShower-Setup";
constexpr char kWifiPassword[] = "camp-shower-setup";
constexpr char kStatusRequest[] = "SHOWER_DISPLAY_V1 STATUS?";
constexpr char kStatusPrefix[] = "SHOWER_STATUS_V1 ";

M5UnitOLED display(kOledSdaPin, kOledSclPin, kI2cFrequencyHz);
WiFiUDP statusUdp;

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
bool udpReady = false;
bool receivedStatus = false;
uint32_t lastButtonChangeMs = 0;
uint32_t lastMessageChangeMs = 0;
uint32_t lastStatusRequestMs = 0;
uint32_t lastStatusReceivedMs = 0;
uint32_t lastWifiAttemptMs = 0;
size_t messageIndex = 0;
DoorState doorState = DoorState::Offline;

void drawCenteredText(const char* text, int32_t y, uint8_t size,
                      uint16_t foreground, uint16_t background) {
  display.setTextDatum(top_center);
  display.setTextColor(foreground, background);
  display.setTextSize(size);
  display.drawString(text, display.width() / 2, y);
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
  display.endWrite();

  Serial.printf("Display: OPEN / %s\n", message);
}

void drawInUseScreen() {
  display.startWrite();
  display.fillScreen(TFT_BLACK);
  drawCenteredText("IN USE", 20, 3, TFT_WHITE, TFT_BLACK);
  display.endWrite();

  Serial.println("Display: IN USE");
}

void drawUnavailableScreen() {
  display.startWrite();
  display.fillScreen(TFT_BLACK);
  drawCenteredText("UNAVAILABLE", 16, 1, TFT_WHITE, TFT_BLACK);
  drawCenteredText("TRY LATER", 36, 2, TFT_WHITE, TFT_BLACK);
  display.endWrite();

  Serial.println("Display: UNAVAILABLE");
}

void drawOfflineScreen() {
  display.startWrite();
  display.fillScreen(TFT_BLACK);
  drawCenteredText(receivedStatus ? "OFFLINE" : "CONNECTING", 14, 2,
                   TFT_WHITE, TFT_BLACK);
  drawCenteredText("TO SHOWER", 42, 1, TFT_WHITE, TFT_BLACK);
  display.endWrite();

  Serial.printf("Display: %s\n", receivedStatus ? "OFFLINE" : "CONNECTING");
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
      // The Tough is authoritative. The button now forces an immediate refresh
      // instead of locally overriding the occupied state.
      lastStatusRequestMs = 0;
      Serial.println("NanoC6 button: requesting fresh status");
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

void beginWifi() {
  lastWifiAttemptMs = millis();
  Serial.printf("Wi-Fi: connecting to %s\n", kWifiName);
  WiFi.begin(kWifiName, kWifiPassword);
}

void serviceWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!udpReady) {
      udpReady = statusUdp.begin(kLocalStatusPort);
      Serial.printf("Wi-Fi: connected, ip=%s gateway=%s udp=%s\n",
                    WiFi.localIP().toString().c_str(),
                    WiFi.gatewayIP().toString().c_str(),
                    udpReady ? "ready" : "failed");
      lastStatusRequestMs = 0;
    }
    return;
  }

  if (udpReady) {
    statusUdp.stop();
    udpReady = false;
  }
  if (millis() - lastWifiAttemptMs >= kWifiRetryIntervalMs) beginWifi();
}

void requestStatus() {
  if (!udpReady || millis() - lastStatusRequestMs < kStatusRequestIntervalMs) return;
  lastStatusRequestMs = millis();
  const IPAddress toughAddress = WiFi.gatewayIP();
  statusUdp.beginPacket(toughAddress, kToughStatusPort);
  statusUdp.write(reinterpret_cast<const uint8_t*>(kStatusRequest), strlen(kStatusRequest));
  statusUdp.endPacket();
}

void receiveStatus() {
  if (!udpReady) return;

  int packetSize = 0;
  while ((packetSize = statusUdp.parsePacket()) > 0) {
    char response[48] = {0};
    const int length = statusUdp.read(
        reinterpret_cast<uint8_t*>(response), sizeof(response) - 1);
    if (length <= 0 || statusUdp.remoteIP() != WiFi.gatewayIP()) continue;
    response[length] = '\0';
    if (strncmp(response, kStatusPrefix, strlen(kStatusPrefix)) != 0) continue;

    const char* state = response + strlen(kStatusPrefix);
    DoorState nextState;
    if (strcmp(state, "OPEN") == 0) nextState = DoorState::Open;
    else if (strcmp(state, "IN_USE") == 0) nextState = DoorState::InUse;
    else if (strcmp(state, "UNAVAILABLE") == 0) nextState = DoorState::Unavailable;
    else continue;

    lastStatusReceivedMs = millis();
    receivedStatus = true;
    setDoorState(nextState);
  }
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
  Serial.println("M5NanoC6 Wi-Fi door display starting");
  Serial.printf("SH1107: SDA GPIO%u, SCL GPIO%u, address 0x3C\n",
                kOledSdaPin, kOledSclPin);

  pinMode(kButtonPin, INPUT_PULLUP);

  if (!display.begin()) {
    Serial.println("ERROR: SH1107 display was not found");
    while (true) {
      delay(1000);
    }
  }

  if (display.width() < display.height()) {
    display.setRotation(display.getRotation() ^ 1);
  }

  Serial.printf("SH1107 ready: %d x %d\n", display.width(), display.height());
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  drawOfflineScreen();
  beginWifi();
  Serial.println("The Tough now controls OPEN / IN USE over local Wi-Fi");
}

void loop() {
  serviceWifi();
  requestStatus();
  receiveStatus();
  enforceStatusTimeout();
  updateButton();
  updateCarousel();
  delay(5);
}
