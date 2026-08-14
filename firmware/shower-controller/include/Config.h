#pragma once

#include <Arduino.h>

namespace Config {

// Tough Grove Port A / rear Tough.EXT I2C connector.
constexpr uint8_t I2C_SDA = 32;
constexpr uint8_t I2C_SCL = 33;
constexpr uint32_t I2C_FREQUENCY = 100000;

constexpr uint8_t PAHUB_ADDRESS = 0x70;
constexpr uint8_t RFID_ADDRESS = 0x28;
constexpr uint8_t RELAY_ADDRESS = 0x26;

// Current prototype wiring: protected flow signal into Port B yellow/GPIO26.
constexpr uint8_t FLOW_PIN = 26;
// Momentary pump toggle: Port C yellow/GPIO14 to GND, active low.
constexpr uint8_t PUMP_BUTTON_PIN = 14;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;

// Tough onboard microSD shares the display SPI bus and has its own CS.
constexpr uint8_t SD_SCK = 18;
constexpr uint8_t SD_MISO = 38;
constexpr uint8_t SD_MOSI = 23;
constexpr uint8_t SD_CS = 4;
constexpr uint32_t SD_FREQUENCY = 10000000;

constexpr uint32_t LOG_INTERVAL_MS = 2000;
constexpr char LOG_PATH[] = "/PULSES.CSV";
constexpr char MEMBER_PATH[] = "/MEMBERS.CSV";
constexpr char SESSION_PATH[] = "/SESSIONS.CSV";
constexpr char SETTINGS_PATH[] = "/SETTINGS.CSV";
constexpr char AUDIO_PATH[] = "/MEXICO.PCM";
constexpr uint32_t AUDIO_SAMPLE_RATE = 44100;
constexpr uint8_t SPEAKER_VOLUME = 55;

constexpr float DEFAULT_ALLOWANCE_GALLONS = 10.0F;
constexpr float DEFAULT_PULSES_PER_GALLON = 450.0F;
constexpr uint8_t PUMP_RELAY = 1;
constexpr uint32_t MAX_SESSION_MS = 20UL * 60UL * 1000UL;
constexpr uint32_t MAX_CALIBRATION_MS = 10UL * 60UL * 1000UL;

// Bench-prototype setup network. Change the password before field use.
constexpr char WIFI_AP_NAME[] = "CampShower-Setup";
constexpr char WIFI_AP_PASSWORD[] = "camp-shower-setup";
constexpr char STATION_NAME[] = "shower-controller-prototype";
constexpr uint16_t DOOR_DISPLAY_PORT = 4210;
constexpr char DOOR_STATUS_REQUEST[] = "SHOWER_DISPLAY_V1 STATUS?";
constexpr char DOOR_STATUS_PREFIX[] = "SHOWER_STATUS_V1 ";
constexpr char ADMIN_USERNAME[] = "admin";
// Used only to initialize a new SD card. Change this before field deployment.
constexpr char INITIAL_ADMIN_PASSWORD[] = "change-me-shower";

}  // namespace Config
