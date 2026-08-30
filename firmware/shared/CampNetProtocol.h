#pragma once

// Camp-wide ESP-NOW wire protocol shared by the M5Stack Tough station firmware
// (firmware/shower-controller) and the M5NanoC6 door sign firmware
// (firmware/door-display). Both projects include this header via
// `build_flags = -I ../shared`; there is exactly one copy of every wire-level
// constant. Keep every packet at or below 250 bytes: the classic ESP32 speaks
// ESP-NOW v1 and only receives v2 frames that fit the v1 payload limit.

#include <stdint.h>
#include <string.h>

namespace CampNet {

// All stations and door signs share one fixed 2.4 GHz channel. The Tough
// soft-AP is pinned to it; the NanoC6 pins its idle STA interface to it.
constexpr uint8_t CHANNEL = 1;
constexpr uint16_t MAGIC = 0xCA5E;
constexpr uint8_t PROTOCOL_VERSION = 2;
// Shared secret for authenticating COMMAND / ACK / AUTH packets (HMAC-SHA256
// truncated to 16 bytes). Change before field use, together with the Wi-Fi
// password; every station and sign must be built with the same value.
constexpr char SECRET[] = "camp-shower-campnet-secret";
constexpr uint8_t MAX_PAYLOAD = 250;
// Station ids are 1..MAX_STATIONS. 0 is reserved for "unknown".
constexpr uint8_t MAX_STATIONS = 8;
constexpr uint8_t UID_BYTES = 10;   // RFID UID, raw bytes (hex string is <= 20 chars)
constexpr uint8_t NAME_BYTES = 33;  // NUL-terminated member name

enum Role : uint8_t { ROLE_SHOWER = 0, ROLE_WATER_FILL = 1, ROLE_RV_FILL = 2, ROLE_COUNT = 3 };
enum DoorState : uint8_t { DOOR_OPEN = 0, DOOR_IN_USE = 1, DOOR_UNAVAILABLE = 2 };
enum PacketType : uint8_t {
  PKT_STATUS = 1,
  PKT_USAGE = 2,
  PKT_MEMBERS = 3,
  PKT_LIMITS = 4,
  PKT_TELEMETRY = 5,  // rich per-station state for the single admin page
  PKT_RECENT = 6,     // last few completed sessions
  PKT_COMMAND = 7,    // admin action addressed to one station (authenticated)
  PKT_ACK = 8,        // result of a COMMAND (authenticated)
  PKT_AUTH = 9,       // admin password salt+hash, versioned (authenticated)
};

// Remote admin actions. Arguments are a short byte string (see each action).
enum CommandAction : uint8_t {
  CMD_ENROLL = 1,             // args: member name (UTF-8, <= 32 bytes)
  CMD_CANCEL_ENROLL = 2,
  CMD_CALIBRATION_START = 3,
  CMD_CALIBRATION_STOP = 4,   // args: float knownGallons
  CMD_MUSIC_CAL_START = 5,
  CMD_MUSIC_CAL_CAPTURE = 6,
  CMD_MUSIC_CAL_CANCEL = 7,
  CMD_AUDIO_TONE = 8,
  CMD_AUDIO_PLAY = 9,
  CMD_AUDIO_STOP = 10,
  CMD_AUDIO_VOLUME = 11,      // args: uint8_t percent
  CMD_SPEAKER_SEARCH = 12,
  CMD_REBOOT = 13,
  CMD_END_SESSION = 14,       // ends the active session; pump goes off
};

enum AckStatus : uint8_t { ACK_OK = 0, ACK_REJECTED = 1, ACK_UNAUTHORIZED = 2, ACK_UNSUPPORTED = 3 };

// End-of-session reasons, compact form of the SESSIONS.CSV reason strings.
enum SessionReason : uint8_t {
  REASON_OTHER = 0, REASON_BUTTON = 1, REASON_LIMIT = 2, REASON_TIMEOUT = 3,
  REASON_HANDOFF = 4, REASON_RELAY_ERROR = 5, REASON_SD_ERROR = 6,
  REASON_SERIAL = 7, REASON_REBOOT = 8,
};

// TelemetryPacket.flags bits.
constexpr uint8_t TELEM_SD_OK = 1 << 0;
constexpr uint8_t TELEM_HUB_OK = 1 << 1;
constexpr uint8_t TELEM_RELAY_OK = 1 << 2;
constexpr uint8_t TELEM_RFID_OK = 1 << 3;
constexpr uint8_t TELEM_CALIBRATION_ACTIVE = 1 << 4;
constexpr uint8_t TELEM_SPEAKER_CONNECTED = 1 << 5;
constexpr uint8_t TELEM_AUDIO_FILE = 1 << 6;
constexpr uint8_t TELEM_MUSIC_CAL_ACTIVE = 1 << 7;
// TelemetryPacket.features bits.
constexpr uint8_t FEATURE_MUSIC = 1 << 0;
constexpr uint8_t FEATURE_LEDS = 1 << 1;
constexpr uint8_t FEATURE_DOOR_SIGN = 1 << 2;
constexpr uint8_t FEATURE_ENROLL_PENDING = 1 << 3;
constexpr uint8_t FEATURE_MUSIC_CALIBRATED = 1 << 4;
constexpr uint8_t FEATURE_PUMP_ON = 1 << 5;

constexpr uint8_t COMMAND_ARG_BYTES = 40;
constexpr uint8_t MAC_BYTES = 16;
constexpr uint8_t RECENT_ENTRIES_PER_PACKET = 8;
constexpr uint8_t MUSIC_POSITIONS = 10;

#pragma pack(push, 1)
struct Header {
  uint16_t magic;
  uint8_t protocolVersion;
  uint8_t type;
  uint8_t stationId;
  uint8_t role;
  uint16_t seq;
};

// Sent by every Tough twice per second and immediately on change. Door signs
// filter on stationId and role.
struct StatusPacket {
  Header header;
  uint8_t doorState;
  uint8_t sessionActive;
  uint32_t uptimeS;
};

// One station's per-wristband lifetime totals. Entries are idempotent upserts
// (totals only grow), so lost chunks heal on the next periodic snapshot.
struct UsageEntry {
  uint8_t uidLen;
  uint8_t uid[UID_BYTES];
  float gallons;
  uint16_t sessions;
};
constexpr uint8_t USAGE_ENTRIES_PER_PACKET = 13;
struct UsagePacket {
  Header header;
  uint8_t chunkIndex;
  uint8_t chunkCount;
  uint8_t entryCount;
  uint8_t reserved;
  uint32_t snapshotVersion;
  UsageEntry entries[USAGE_ENTRIES_PER_PACKET];
};

// Whole member registry, chunked. Version-numbered last-writer-wins.
struct MemberEntry {
  uint8_t uidLen;
  uint8_t uid[UID_BYTES];
  char name[NAME_BYTES];
  float allowance;  // 0 = use the station's role limit
  uint8_t enabled;
};
constexpr uint8_t MEMBER_ENTRIES_PER_PACKET = 4;
struct MembersPacket {
  Header header;
  uint8_t chunkIndex;
  uint8_t chunkCount;
  uint8_t entryCount;
  uint8_t totalMembers;
  uint32_t version;
  MemberEntry entries[MEMBER_ENTRIES_PER_PACKET];
};

// Per-role session limits (shower, water fill, RV fill). Version-numbered.
struct LimitsPacket {
  Header header;
  uint32_t version;
  float gallons[ROLE_COUNT];
  uint16_t minutes[ROLE_COUNT];
};

// Everything the admin page needs to render one station's cards. Sent by
// every Tough every 2 s and immediately when something the page shows changes.
struct TelemetryPacket {
  Header header;
  uint32_t uptimeS;
  uint32_t freeHeap;
  uint32_t minFreeHeap;
  uint32_t audioUnderruns;
  uint32_t calibrationPulses;
  float pulsesPerGallon;
  float sessionGallons;
  float sessionLimit;
  uint16_t musicKnobRaw;
  uint16_t musicPositions[MUSIC_POSITIONS];
  uint8_t flags;      // TELEM_* bits
  uint8_t features;   // FEATURE_* bits
  uint8_t doorState;
  uint8_t wifiClients;
  uint8_t speakerVolume;
  int8_t musicChannel;
  uint8_t musicCalNext;
  uint8_t reserved;
  char activeName[33];
  char pendingName[33];
  char speaker[16];         // SpeakerAudio::connectionLabel()
  char playback[24];        // SpeakerAudio::playbackLabel()
  char calibrationMessage[36];
  char message[32];         // AdminServer lastMessage_
};

struct RecentEntry {
  uint8_t uidLen;
  uint8_t uid[UID_BYTES];
  float gallons;
  uint16_t durationS;
  uint8_t reason;  // SessionReason
};
struct RecentPacket {
  Header header;
  uint8_t count;
  RecentEntry entries[RECENT_ENTRIES_PER_PACKET];
};

// Admin action for one station. mac = HMAC-SHA256(SECRET, bytes of the packet
// from `header.stationId` up to but not including `mac`) truncated to 16
// bytes; the same rule signs AckPacket and AuthPacket. Receivers drop packets
// with a bad mac and remember recent (sender, nonce) pairs to reject
// replays/duplicates.
struct CommandPacket {
  Header header;
  uint8_t target;    // station id that must execute this
  uint8_t action;    // CommandAction
  uint8_t argLen;
  uint8_t reserved;
  uint32_t nonce;    // random per request; retries reuse it
  uint8_t args[COMMAND_ARG_BYTES];
  uint8_t mac[MAC_BYTES];
};

struct AckPacket {
  Header header;
  uint8_t target;    // station id of the original sender
  uint8_t status;    // AckStatus
  uint8_t reserved[2];
  uint32_t nonce;    // echoes CommandPacket.nonce
  char message[48];
  uint8_t mac[MAC_BYTES];
};

// Admin password (salted SHA-256 as stored in SETTINGS.CSV), version-numbered
// like limits. Authenticated the same way as CommandPacket.
struct AuthPacket {
  Header header;
  uint32_t version;
  uint8_t salt[16];
  uint8_t hash[32];
  uint8_t mac[MAC_BYTES];
};
#pragma pack(pop)

static_assert(sizeof(Header) == 8, "header layout");
static_assert(sizeof(UsageEntry) == 17, "usage entry layout");
static_assert(sizeof(MemberEntry) == 49, "member entry layout");
static_assert(sizeof(StatusPacket) <= MAX_PAYLOAD, "status packet too large");
static_assert(sizeof(UsagePacket) <= MAX_PAYLOAD, "usage packet too large");
static_assert(sizeof(MembersPacket) <= MAX_PAYLOAD, "members packet too large");
static_assert(sizeof(LimitsPacket) <= MAX_PAYLOAD, "limits packet too large");
static_assert(sizeof(TelemetryPacket) <= MAX_PAYLOAD, "telemetry packet too large");
static_assert(sizeof(RecentPacket) <= MAX_PAYLOAD, "recent packet too large");
static_assert(sizeof(CommandPacket) <= MAX_PAYLOAD, "command packet too large");
static_assert(sizeof(AckPacket) <= MAX_PAYLOAD, "ack packet too large");
static_assert(sizeof(AuthPacket) <= MAX_PAYLOAD, "auth packet too large");

inline const char* sessionReasonName(uint8_t reason) {
  switch (reason) {
    case REASON_BUTTON: return "BUTTON";
    case REASON_LIMIT: return "LIMIT";
    case REASON_TIMEOUT: return "TIMEOUT";
    case REASON_HANDOFF: return "HANDOFF";
    case REASON_RELAY_ERROR: return "RELAY_ERROR";
    case REASON_SD_ERROR: return "SD_ERROR";
    case REASON_SERIAL: return "SERIAL";
    case REASON_REBOOT: return "REBOOT";
    default: return "OTHER";
  }
}

inline uint8_t sessionReasonCode(const char* reason) {
  if (reason == nullptr) return REASON_OTHER;
  for (uint8_t code = REASON_BUTTON; code <= REASON_REBOOT; ++code) {
    if (strcmp(reason, sessionReasonName(code)) == 0) return code;
  }
  return REASON_OTHER;
}

inline bool headerValid(const uint8_t* data, int length) {
  if (data == nullptr || length < static_cast<int>(sizeof(Header))) return false;
  Header header;
  memcpy(&header, data, sizeof(header));
  return header.magic == MAGIC && header.protocolVersion == PROTOCOL_VERSION &&
         header.stationId >= 1 && header.stationId <= MAX_STATIONS &&
         header.role < ROLE_COUNT;
}

inline const char* roleName(uint8_t role) {
  switch (role) {
    case ROLE_SHOWER: return "Shower";
    case ROLE_WATER_FILL: return "Water Fill";
    case ROLE_RV_FILL: return "RV Fill";
    default: return "Unknown";
  }
}

inline const char* doorStateName(uint8_t state) {
  switch (state) {
    case DOOR_OPEN: return "OPEN";
    case DOOR_IN_USE: return "IN_USE";
    case DOOR_UNAVAILABLE: return "UNAVAILABLE";
    default: return "?";
  }
}

// Hex UID string ("04A1B2...") <-> raw bytes. Returns the byte count (0 on error).
inline uint8_t uidFromHex(const char* hex, uint8_t out[UID_BYTES]) {
  memset(out, 0, UID_BYTES);
  if (hex == nullptr) return 0;
  const size_t length = strlen(hex);
  if (length == 0 || length % 2 != 0 || length / 2 > UID_BYTES) return 0;
  for (size_t i = 0; i < length; ++i) {
    const char c = hex[i];
    uint8_t nibble;
    if (c >= '0' && c <= '9') nibble = c - '0';
    else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
    else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
    else return 0;
    out[i / 2] = static_cast<uint8_t>((out[i / 2] << 4) | nibble);
  }
  return static_cast<uint8_t>(length / 2);
}

inline void uidToHex(const uint8_t* bytes, uint8_t length, char out[UID_BYTES * 2 + 1]) {
  static const char digits[] = "0123456789ABCDEF";
  if (length > UID_BYTES) length = UID_BYTES;
  for (uint8_t i = 0; i < length; ++i) {
    out[i * 2] = digits[bytes[i] >> 4];
    out[i * 2 + 1] = digits[bytes[i] & 0x0F];
  }
  out[length * 2] = '\0';
}

}  // namespace CampNet
