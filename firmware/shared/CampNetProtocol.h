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
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint8_t MAX_PAYLOAD = 250;
// Station ids are 1..MAX_STATIONS. 0 is reserved for "unknown".
constexpr uint8_t MAX_STATIONS = 8;
constexpr uint8_t UID_BYTES = 10;   // RFID UID, raw bytes (hex string is <= 20 chars)
constexpr uint8_t NAME_BYTES = 33;  // NUL-terminated member name

enum Role : uint8_t { ROLE_SHOWER = 0, ROLE_WATER_FILL = 1, ROLE_RV_FILL = 2, ROLE_COUNT = 3 };
enum DoorState : uint8_t { DOOR_OPEN = 0, DOOR_IN_USE = 1, DOOR_UNAVAILABLE = 2 };
enum PacketType : uint8_t { PKT_STATUS = 1, PKT_USAGE = 2, PKT_MEMBERS = 3, PKT_LIMITS = 4 };

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
#pragma pack(pop)

static_assert(sizeof(Header) == 8, "header layout");
static_assert(sizeof(UsageEntry) == 17, "usage entry layout");
static_assert(sizeof(MemberEntry) == 49, "member entry layout");
static_assert(sizeof(StatusPacket) <= MAX_PAYLOAD, "status packet too large");
static_assert(sizeof(UsagePacket) <= MAX_PAYLOAD, "usage packet too large");
static_assert(sizeof(MembersPacket) <= MAX_PAYLOAD, "members packet too large");
static_assert(sizeof(LimitsPacket) <= MAX_PAYLOAD, "limits packet too large");

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
