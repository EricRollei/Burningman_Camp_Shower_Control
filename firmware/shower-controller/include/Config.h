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
// Momentary pump toggle: Port C yellow/GPIO14 to GND, active low.
constexpr uint8_t PUMP_BUTTON_PIN = 14;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;

// Addressable shower lighting: Port C white/GPIO13 drives the 12 V WS2811-style
// strip data input. Strip power is separate; its negative bus is shared with
// Tough. Count addressable three-LED groups, not individual LED packages.
constexpr uint8_t LED_STRIP_PIN = 13;
constexpr uint16_t LED_STRIP_COUNT = 300;
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
// effective 0-127 scale; 43% preserves the previous raw setting of 55.
constexpr uint8_t DEFAULT_SPEAKER_VOLUME_PERCENT = 43;

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
