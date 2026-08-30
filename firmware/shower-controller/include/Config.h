#pragma once

#include <Arduino.h>

#include "CampNetProtocol.h"

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
// Music selector: Port B white/GPIO36. Potentiometer ends go to 3V3 and GND;
// never use the Port B 5 V pin as the ADC reference.
constexpr uint8_t MUSIC_KNOB_PIN = 36;
constexpr uint16_t MUSIC_KNOB_ON_RAW = 1024;   // About 25% of 12-bit range.
constexpr uint16_t MUSIC_KNOB_OFF_RAW = 700;   // Hysteresis prevents chatter.
constexpr uint32_t MUSIC_KNOB_SAMPLE_MS = 20;
constexpr uint32_t MUSIC_KNOB_RETRY_MS = 3000;
constexpr uint8_t MUSIC_KNOB_POSITION_COUNT = 10;  // 0=quiet, 1-9=channels.
constexpr const char* MUSIC_CHANNEL_PATHS[MUSIC_KNOB_POSITION_COUNT] = {
    "", "/CH1.PCM", "/CH2.PCM", "/CH3.PCM", "/CH4.PCM",
    "/CH5.PCM", "/CH6.PCM", "/CH7.PCM", "/CH8.PCM", "/CH9.PCM"};
constexpr const char* MUSIC_CHANNEL_NAMES[MUSIC_KNOB_POSITION_COUNT] = {
    "Quiet", "Purple Rain", "Africa", "Whose Bed Have Your Boots Been Under",
    "It's My House", "Dancing Queen", "What a Feeling", "Footloose",
    "Maniac", "Jesus Built My Hotrod"};
constexpr uint16_t MUSIC_KNOB_MIN_CALIBRATION_SPAN = 1000;
constexpr uint16_t MUSIC_KNOB_MIN_POINT_SPACING = 12;
constexpr uint16_t MUSIC_KNOB_CHANNEL_HYSTERESIS = 28;
// The installed D-taper pot showed up to ~38 counts of stationary ADC wander.
// Stay above that floor while remaining far below adjacent calibrated notches.
constexpr uint16_t MUSIC_KNOB_MOTION_THRESHOLD = 64;
constexpr uint32_t MUSIC_KNOB_SETTLE_MS = 100;
// Momentary shower button: Port C yellow/GPIO14 to GND, active low.
// First press starts the water; second press ends the shower.
constexpr uint8_t PUMP_BUTTON_PIN = 14;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
constexpr uint32_t BUTTON_REBOOT_HOLD_MS = 5000;
// How long the usage summary stays on screen after a shower ends.
constexpr uint32_t SUMMARY_DISPLAY_MS = 10000;

// Addressable shower lighting: Port C white/GPIO13 drives the 12 V strip's
// WS2812-compatible data input. Strip power is separate; its negative bus is
// shared with Tough. Bench testing confirms 120 individually addressable
// pixels: a 40-pixel frame drove almost exactly one-third of the 2 m strip.
constexpr uint8_t LED_STRIP_PIN = 13;
constexpr uint16_t LED_STRIP_COUNT = 120;
constexpr uint8_t LED_STRIP_BRIGHTNESS = 96;
constexpr uint32_t LED_ANIMATION_INTERVAL_MS = 30;
constexpr uint8_t LED_RAINBOW_SPACING = 3;
constexpr uint8_t LED_RAINBOW_SPEED = 2;

// Tough onboard microSD shares the display SPI bus and has its own CS.
constexpr uint8_t SD_SCK = 18;
constexpr uint8_t SD_MISO = 38;
constexpr uint8_t SD_MOSI = 23;
constexpr uint8_t SD_CS = 4;
constexpr uint32_t SD_FREQUENCY = 10000000;

constexpr uint32_t LOG_INTERVAL_MS = 2000;
constexpr char LOG_PATH[] = "/PULSES.CSV";
// Per-tag totals snapshot so boot only replays the log tail, not the whole
// file (which grows all week).
constexpr char PULSE_SNAPSHOT_PATH[] = "/PULSETOT.CSV";
constexpr char MEMBER_PATH[] = "/MEMBERS.CSV";
constexpr char SESSION_PATH[] = "/SESSIONS.CSV";
constexpr char SETTINGS_PATH[] = "/SETTINGS.CSV";
// Manual dashboard playback/upload targets channel 1.
constexpr char AUDIO_PATH[] = "/CH1.PCM";
constexpr uint32_t AUDIO_SAMPLE_RATE = 44100;
// Song PCM is staged into RAM by the main loop so the Bluetooth task never
// touches the SD card (the SD/SPI stack is not safe across tasks). 128 KiB of
// PSRAM holds ~740 ms of 44.1 kHz stereo audio.
constexpr size_t AUDIO_BUFFER_BYTES = 131072;
constexpr size_t AUDIO_REFILL_CHUNK_BYTES = 8192;

// Bluetooth discovery is radio-hostile to the WiFi AP: scan continuously only
// for a short window, then retry one inquiry round on a slow interval (or on
// demand from the admin page).
constexpr uint32_t BT_DISCOVERY_ACTIVE_MS = 2UL * 60UL * 1000UL;
constexpr uint32_t BT_DISCOVERY_RETRY_INTERVAL_MS = 5UL * 60UL * 1000UL;

// Loop-task watchdog: a wedged main loop reboots instead of staying dead.
constexpr uint32_t WDT_TIMEOUT_S = 20;
constexpr uint32_t HEALTH_LOG_INTERVAL_MS = 30000;
constexpr uint32_t ADMIN_RETRY_INTERVAL_MS = 30000;
// Human-facing percentage. SpeakerAudio maps this to the A2DP library's
// effective 0-127 PCM scale. Full scale leaves the source PCM unchanged;
// use the speaker's physical buttons for its amplifier volume.
constexpr uint8_t LEGACY_DEFAULT_SPEAKER_VOLUME_PERCENT = 43;
constexpr uint8_t DEFAULT_SPEAKER_VOLUME_PERCENT = 100;
constexpr uint8_t SPEAKER_VOLUME_SETTINGS_VERSION = 1;

// Station identity comes from the PlatformIO environment (-DSTATION_ID=n
// -DSTATION_ROLE=r); see platformio.ini. Everything else derives from it.
#ifndef STATION_ID
#define STATION_ID 1
#endif
#ifndef STATION_ROLE
#define STATION_ROLE 0
#endif
static_assert(STATION_ID >= 1 && STATION_ID <= CampNet::MAX_STATIONS, "STATION_ID out of range");
static_assert(STATION_ROLE >= 0 && STATION_ROLE < CampNet::ROLE_COUNT, "STATION_ROLE out of range");
constexpr uint8_t STATION_ID_VALUE = STATION_ID;
constexpr uint8_t STATION_ROLE_VALUE = STATION_ROLE;
constexpr bool IS_SHOWER = STATION_ROLE_VALUE == CampNet::ROLE_SHOWER;
// Fill stations share the controller stack but have no speaker, music knob,
// LED strip, or door sign.
constexpr bool HAS_MUSIC = IS_SHOWER;
constexpr bool HAS_LED_STRIP = IS_SHOWER;
constexpr bool HAS_DOOR_SIGN = IS_SHOWER;
// Indexed by STATION_ID (index 0 unused).
constexpr const char* STATION_NAMES[CampNet::MAX_STATIONS + 1] = {
    "", "Shower 1", "Shower 2", "Water Fill", "RV Fill",
    "Station 5", "Station 6", "Station 7", "Station 8"};
constexpr const char* STATION_NAME = STATION_NAMES[STATION_ID_VALUE];
// Indexed by role: shower, water fill, RV fill.
constexpr const char* ROLE_HEADER_LABELS[CampNet::ROLE_COUNT] = {"CAMP SHOWER", "WATER FILL", "RV FILL"};
constexpr const char* ROLE_ACTIVE_LABELS[CampNet::ROLE_COUNT] = {"SHOWER IN PROGRESS", "FILLING JUGS", "FILLING RV"};
constexpr const char* ROLE_COMPLETE_LABELS[CampNet::ROLE_COUNT] = {"SHOWER COMPLETE", "FILL COMPLETE", "FILL COMPLETE"};
constexpr const char* ROLE_SESSION_NOUNS[CampNet::ROLE_COUNT] = {"shower", "fill", "fill"};
constexpr const char* ROLE_IDLE_PROMPTS[CampNet::ROLE_COUNT] = {
    "TO START A SHOWER", "TO FILL YOUR JUGS", "TO FILL YOUR RV"};
constexpr const char* ROLE_OPEN_LABELS[CampNet::ROLE_COUNT] = {
    "SHOWER OPEN", "WATER FILL OPEN", "RV FILL OPEN"};
constexpr const char* ROLE_USED_LABELS[CampNet::ROLE_COUNT] = {
    "USED THIS SHOWER", "USED THIS FILL", "USED THIS FILL"};
constexpr const char* ROLE_THANKS_LABELS[CampNet::ROLE_COUNT] = {
    "THANKS FOR SHOWERING", "THANKS FOR FILLING", "THANKS FOR FILLING"};

// Per-role session limits. Defaults seed a fresh SD card; the admin page edits
// them and the values sync to every station over CampNet. Bounds are hard.
constexpr float DEFAULT_ROLE_LIMIT_GALLONS[CampNet::ROLE_COUNT] = {10.0F, 10.0F, 100.0F};
constexpr uint16_t DEFAULT_ROLE_LIMIT_MINUTES[CampNet::ROLE_COUNT] = {20, 60, 60};
constexpr float MIN_LIMIT_GALLONS = 0.5F;
constexpr float MAX_LIMIT_GALLONS = 500.0F;
constexpr uint16_t MIN_LIMIT_MINUTES = 1;
constexpr uint16_t MAX_LIMIT_MINUTES = 180;
// A member's own allowance of 0 means "use the station's role limit".
constexpr float DEFAULT_ALLOWANCE_GALLONS = 0.0F;
constexpr float DEFAULT_PULSES_PER_GALLON = 450.0F;
// Fresh/upgraded cards retain the existing pump wiring. Auxiliary outputs are
// intentionally unmapped until an admin assigns physical relay channels.
constexpr uint8_t DEFAULT_PUMP_RELAY = 1;
constexpr uint8_t DEFAULT_CHARGER_RELAY = 0;
constexpr uint8_t DEFAULT_ACCESSORY_RELAY = 0;
constexpr bool DEFAULT_ACCESSORY_ENABLED = true;
constexpr uint32_t RELAY_TEST_MS = 5000;
constexpr uint32_t MAX_CALIBRATION_MS = 10UL * 60UL * 1000UL;

// CampNet: ESP-NOW broadcast between all stations and door signs. Cadences are
// periodic idempotent snapshots so lost frames self-heal.
constexpr char NET_USAGE_PATH[] = "/NETUSAGE.CSV";
constexpr char MEMBER_VERSION_PATH[] = "/MEMBERS.VER";
constexpr uint32_t NET_STATUS_INTERVAL_MS = 2000;
// USAGE/MEMBERS/LIMITS/AUTH periodic resends are skipped while every online
// peer already holds the same version/content; NET_USAGE_REFRESH_MS is the
// unconditional fallback so a lost usage chunk still heals.
constexpr uint32_t NET_USAGE_INTERVAL_MS = 30000;
constexpr uint32_t NET_USAGE_REFRESH_MS = 300000;
constexpr uint32_t NET_MEMBERS_INTERVAL_MS = 30000;
constexpr uint32_t NET_LIMITS_INTERVAL_MS = 30000;
constexpr uint32_t NET_AUTH_INTERVAL_MS = 30000;
// Single admin page: telemetry every 2 s (and on change, rate-floored so a
// chatty status line cannot flood the tx ring), recent sessions every 30 s.
constexpr uint32_t NET_TELEMETRY_INTERVAL_MS = 2000;
constexpr uint32_t NET_TELEMETRY_MIN_INTERVAL_MS = 200;
constexpr uint32_t NET_RECENT_INTERVAL_MS = 30000;
// Remote admin commands: resend until ACKed, then keep the result around for
// the HTTP poller. Receivers remember (sender, nonce) pairs to drop replays
// and answer duplicates with the cached ACK, which is itself sent twice.
constexpr uint32_t NET_COMMAND_RETRY_MS = 400;
constexpr uint8_t NET_COMMAND_ATTEMPTS = 6;
constexpr uint32_t NET_COMMAND_RESULT_TTL_MS = 30000;
constexpr uint32_t NET_COMMAND_REPLAY_WINDOW_MS = 60000;
constexpr uint32_t NET_ACK_REPEAT_MS = 250;
// Bluetooth inquiry can occupy the shared radio for about 13 seconds. Allow
// missed status packets across a complete inquiry without making a healthy
// controller flicker offline in the admin dashboard. Door signs retain their
// independent, shorter fail-safe timeout.
constexpr uint32_t NET_PEER_TIMEOUT_MS = 45000;
constexpr uint32_t NET_STAGING_TIMEOUT_MS = 5000;
constexpr uint32_t NET_LEDGER_SAVE_DEBOUNCE_MS = 15000;
constexpr uint32_t NET_BOOT_ANNOUNCE_DELAY_MS = 2000;
constexpr uint8_t WIFI_AP_MAX_CLIENTS = 8;

// Every station runs a soft-AP with the same name and password, pinned to
// CampNet::CHANNEL, so a phone auto-joins whichever is nearest and
// http://192.168.4.1/ works everywhere. The Wi-Fi password is the only gate:
// the admin page itself is open (ADMIN_PAGE_PASSWORD = false).
constexpr char WIFI_AP_NAME[] = "CampShower";
constexpr char WIFI_AP_PASSWORD[] = "dustybutthole";
constexpr bool ADMIN_PAGE_PASSWORD = false;
constexpr char ADMIN_USERNAME[] = "admin";
// Used only to initialize a new SD card. Change this before field deployment.
constexpr char INITIAL_ADMIN_PASSWORD[] = "change-me-shower";

}  // namespace Config
