#include "CampNet.h"

#include <WiFi.h>
#include <esp_system.h>
#include <mbedtls/md.h>

#include <stddef.h>

#include "Config.h"
#include "PsramAlloc.h"

CampNetLink* CampNetLink::instance_ = nullptr;

namespace {

// HMAC-SHA256(SECRET, data) truncated to MAC_BYTES.
void computeMac(const uint8_t* data, size_t length, uint8_t out[CampNet::MAC_BYTES]) {
  uint8_t full[32] = {0};
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info != nullptr) {
    mbedtls_md_hmac(info, reinterpret_cast<const unsigned char*>(CampNet::SECRET),
                    strlen(CampNet::SECRET), data, length, full);
  }
  memcpy(out, full, CampNet::MAC_BYTES);
}

bool macEquals(const uint8_t* a, const uint8_t* b) {
  uint8_t difference = 0;
  for (size_t i = 0; i < CampNet::MAC_BYTES; ++i) difference |= a[i] ^ b[i];
  return difference == 0;
}

// The authenticated span of each packet: from `first` up to (not including)
// the mac field, so the header (seq, sender) stays outside the mac.
template <typename Packet>
void signPacket(Packet& packet, size_t first) {
  const size_t macOffset = offsetof(Packet, mac);
  uint8_t* bytes = reinterpret_cast<uint8_t*>(&packet);
  computeMac(bytes + first, macOffset - first, bytes + macOffset);
}

template <typename Packet>
bool packetAuthentic(const Packet& packet, size_t first) {
  const size_t macOffset = offsetof(Packet, mac);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&packet);
  uint8_t mac[CampNet::MAC_BYTES];
  computeMac(bytes + first, macOffset - first, mac);
  return macEquals(mac, bytes + macOffset);
}

// Sign from the sender id onward (stationId, role, seq, then the body) so a
// captured command cannot be replayed under a different sender id to slip
// past the per-sender nonce dedupe. Retries re-enqueue the identical bytes.
constexpr size_t SIGNED_FROM = offsetof(CampNet::Header, stationId);
constexpr size_t COMMAND_SIGNED_FROM = SIGNED_FROM;
constexpr size_t ACK_SIGNED_FROM = SIGNED_FROM;
constexpr size_t AUTH_SIGNED_FROM = SIGNED_FROM;

// Telemetry equality ignores the header and the counters that tick every
// loop; otherwise every packet would look "changed".
void normalizeTelemetry(CampNet::TelemetryPacket& packet) {
  memset(reinterpret_cast<uint8_t*>(&packet), 0, sizeof(CampNet::Header));
  packet.uptimeS = 0;
  packet.freeHeap = 0;
  packet.minFreeHeap = 0;
  packet.audioUnderruns = 0;
  packet.wifiClients = 0;
  packet.musicKnobRaw = 0;
  packet.calibrationPulses = 0;
  packet.sessionGallons = 0.0F;
}

void normalizeRecent(CampNet::RecentPacket& packet) {
  memset(reinterpret_cast<uint8_t*>(&packet), 0, sizeof(CampNet::Header));
}

size_t recentPacketLength(uint8_t count) {
  if (count > CampNet::RECENT_ENTRIES_PER_PACKET) count = CampNet::RECENT_ENTRIES_PER_PACKET;
  return sizeof(CampNet::RecentPacket) -
         sizeof(CampNet::RecentEntry) * (CampNet::RECENT_ENTRIES_PER_PACKET - count);
}

}  // namespace

CampNetLink::CampNetLink(MemberRegistry& members, const SessionStorage& sessions,
                 SettingsStore& settings, UsageLedger& ledger)
    : members_(members), sessions_(sessions), settings_(settings), ledger_(ledger) {}

bool CampNetLink::begin() {
  if (ready_) return true;
  instance_ = this;
  bootMs_ = millis();

  if (tx_ == nullptr) tx_ = psramArray<Frame>(TX_RING);
  if (remoteTelemetry_ == nullptr) remoteTelemetry_ = psramArray<RemoteTelemetry>(CampNet::MAX_STATIONS + 1);
  if (remoteRecent_ == nullptr) remoteRecent_ = psramArray<RemoteRecent>(CampNet::MAX_STATIONS + 1);
  if (staging_ == nullptr) staging_ = psramArray<CampNet::MemberEntry>(MemberRegistry::MAX_MEMBERS);
  if (snapshot_ == nullptr) snapshot_ = psramArray<CampNet::MemberEntry>(MemberRegistry::MAX_MEMBERS);
  if (tx_ == nullptr || remoteTelemetry_ == nullptr || remoteRecent_ == nullptr ||
      staging_ == nullptr || snapshot_ == nullptr) {
    Serial.println("[NET] buffer allocation failed");
    return false;
  }

  if (!apReady_) {
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    // The AP is pinned to the shared channel so ESP-NOW frames from every
    // other device land on this radio. Never retune after start.
    apReady_ = WiFi.softAP(Config::WIFI_AP_NAME, Config::WIFI_AP_PASSWORD,
                           CampNet::CHANNEL, 0, Config::WIFI_AP_MAX_CLIENTS);
    if (!apReady_) return false;
  }

  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(&CampNetLink::onReceive);
  esp_now_register_send_cb(&CampNetLink::onSent);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, CampNet::BROADCAST_MAC, sizeof(peer.peer_addr));
  peer.ifidx = WIFI_IF_AP;
  peer.channel = 0;  // follow the soft-AP channel
  peer.encrypt = false;
  if (!esp_now_is_peer_exist(CampNet::BROADCAST_MAC) && esp_now_add_peer(&peer) != ESP_OK) {
    esp_now_deinit();
    return false;
  }
  ready_ = true;
  return true;
}

String CampNetLink::address() const {
  return apReady_ ? WiFi.softAPIP().toString() : String("offline");
}

void CampNetLink::onReceive(CAMPNET_RECV_CB_PARAMS) {
  CampNetLink* self = instance_;
  if (self == nullptr || data == nullptr || length <= 0 ||
      length > static_cast<int>(CampNet::MAX_PAYLOAD)) return;
  // Wi-Fi task context: copy into the ring and get out.
  portENTER_CRITICAL(&self->rxMux_);
  const size_t next = (self->rxHead_ + 1) % RX_RING;
  if (next == self->rxTail_) {
    ++self->rxDropped_;
  } else {
    Frame& frame = self->rx_[self->rxHead_];
    frame.length = static_cast<uint8_t>(length);
    memcpy(frame.data, data, static_cast<size_t>(length));
    self->rxHead_ = next;
  }
  portEXIT_CRITICAL(&self->rxMux_);
}

void CampNetLink::onSent(CAMPNET_SEND_CB_PARAMS) {
  CampNetLink* self = instance_;
  if (self == nullptr) return;
  if (status != ESP_NOW_SEND_SUCCESS) ++self->txFailures_;
  self->txInFlight_ = false;
}

void CampNetLink::fillHeader(CampNet::Header& header, uint8_t type) {
  header.magic = CampNet::MAGIC;
  header.protocolVersion = CampNet::PROTOCOL_VERSION;
  header.type = type;
  header.stationId = Config::STATION_ID_VALUE;
  header.role = Config::STATION_ROLE_VALUE;
  header.seq = seq_++;
}

bool CampNetLink::enqueue(const void* data, size_t length) {
  if (!ready_ || length == 0 || length > CampNet::MAX_PAYLOAD) return false;
  const size_t next = (txHead_ + 1) % TX_RING;
  if (next == txTail_) return false;  // queue full; periodic resend covers it
  Frame& frame = tx_[txHead_];
  frame.length = static_cast<uint8_t>(length);
  memcpy(frame.data, data, length);
  txHead_ = next;
  return true;
}

void CampNetLink::pumpTx() {
  if (txInFlight_) {
    if (millis() - txStartedMs_ < TX_TIMEOUT_MS) return;
    txInFlight_ = false;  // callback never came; do not stall forever
  }
  if (txTail_ == txHead_) return;
  Frame& frame = tx_[txTail_];
  txTail_ = (txTail_ + 1) % TX_RING;
  txInFlight_ = true;
  txStartedMs_ = millis();
  if (esp_now_send(CampNet::BROADCAST_MAC, frame.data, frame.length) == ESP_OK) {
    ++txPackets_;
  } else {
    ++txFailures_;
    txInFlight_ = false;
  }
}

void CampNetLink::drainRx() {
  for (uint8_t budget = 0; budget < RX_RING; ++budget) {
    Frame frame;
    portENTER_CRITICAL(&rxMux_);
    if (rxTail_ == rxHead_) {
      portEXIT_CRITICAL(&rxMux_);
      return;
    }
    frame = rx_[rxTail_];
    rxTail_ = (rxTail_ + 1) % RX_RING;
    portEXIT_CRITICAL(&rxMux_);
    ++rxPackets_;
    dispatch(frame.data, frame.length);
  }
}

void CampNetLink::dispatch(const uint8_t* data, int length) {
  if (!CampNet::headerValid(data, length)) return;
  CampNet::Header header;
  memcpy(&header, data, sizeof(header));
  if (header.stationId == Config::STATION_ID_VALUE) {
    // Another device is flashed with our id. Ignore it, but make it visible.
    if (millis() - lastDuplicateIdLogMs_ > 10000) {
      lastDuplicateIdLogMs_ = millis();
      Serial.printf("[NET] WARNING: another station is using id %u\n", header.stationId);
    }
    return;
  }
  const uint32_t now = millis();
  Peer& peer = peers_[header.stationId];
  const bool firstSighting = !peer.seen;
  const bool returned = peer.seen && now - peer.lastSeenMs >= Config::NET_PEER_TIMEOUT_MS;
  peer.seen = true;
  peer.role = header.role;
  peer.lastSeenMs = now;
  if (firstSighting || returned) {
    Serial.printf("[NET] peer %u (%s) %s\n", header.stationId, CampNet::roleName(header.role),
                  firstSighting ? "online" : "back online");
    // A newcomer (or a reboot) should hear our state right away, whatever
    // version it last told us about.
    peer.membersVersion = peer.limitsVersion = peer.authVersion = 0;
    usageDirty_ = membersDirty_ = limitsDirty_ = authDirty_ = true;
    telemetryDirty_ = recentDirty_ = true;
  }

  switch (header.type) {
    case CampNet::PKT_STATUS: {
      if (length < static_cast<int>(sizeof(CampNet::StatusPacket))) return;
      CampNet::StatusPacket packet;
      memcpy(&packet, data, sizeof(packet));
      peer.doorState = packet.doorState;
      peer.sessionActive = packet.sessionActive != 0;
      peer.uptimeS = packet.uptimeS;
      break;
    }
    case CampNet::PKT_USAGE: {
      if (length < static_cast<int>(sizeof(CampNet::UsagePacket) -
                                    sizeof(CampNet::UsageEntry) * CampNet::USAGE_ENTRIES_PER_PACKET)) return;
      CampNet::UsagePacket packet = {};
      memcpy(&packet, data, min(static_cast<size_t>(length), sizeof(packet)));
      handleUsage(packet, length);
      break;
    }
    case CampNet::PKT_MEMBERS: {
      if (length < static_cast<int>(sizeof(CampNet::MembersPacket) -
                                    sizeof(CampNet::MemberEntry) * CampNet::MEMBER_ENTRIES_PER_PACKET)) return;
      CampNet::MembersPacket packet = {};
      memcpy(&packet, data, min(static_cast<size_t>(length), sizeof(packet)));
      handleMembers(packet, length);
      break;
    }
    case CampNet::PKT_LIMITS: {
      if (length < static_cast<int>(sizeof(CampNet::LimitsPacket))) return;
      CampNet::LimitsPacket packet;
      memcpy(&packet, data, sizeof(packet));
      handleLimits(packet);
      break;
    }
    case CampNet::PKT_TELEMETRY: {
      if (length < static_cast<int>(CampNet::TELEMETRY_LEGACY_BYTES)) return;
      CampNet::TelemetryPacket packet = {};
      memcpy(&packet, data, min(static_cast<size_t>(length), sizeof(packet)));
      handleTelemetry(packet);
      break;
    }
    case CampNet::PKT_RECENT: {
      if (length < static_cast<int>(recentPacketLength(0))) return;
      CampNet::RecentPacket packet = {};
      memcpy(&packet, data, min(static_cast<size_t>(length), sizeof(packet)));
      handleRecent(packet, length);
      break;
    }
    case CampNet::PKT_COMMAND: {
      if (length < static_cast<int>(sizeof(CampNet::CommandPacket))) {
        ++rxRejected_;
        return;
      }
      CampNet::CommandPacket packet;
      memcpy(&packet, data, sizeof(packet));
      handleCommand(packet);
      break;
    }
    case CampNet::PKT_ACK: {
      if (length < static_cast<int>(sizeof(CampNet::AckPacket))) {
        ++rxRejected_;
        return;
      }
      CampNet::AckPacket packet;
      memcpy(&packet, data, sizeof(packet));
      handleAck(packet);
      break;
    }
    case CampNet::PKT_AUTH: {
      if (length < static_cast<int>(sizeof(CampNet::AuthPacket))) {
        ++rxRejected_;
        return;
      }
      CampNet::AuthPacket packet;
      memcpy(&packet, data, sizeof(packet));
      handleAuth(packet);
      break;
    }
    default:
      break;
  }
}

void CampNetLink::handleUsage(const CampNet::UsagePacket& packet, int length) {
  const size_t headerBytes = sizeof(CampNet::UsagePacket) -
                             sizeof(CampNet::UsageEntry) * CampNet::USAGE_ENTRIES_PER_PACKET;
  const size_t carried = (static_cast<size_t>(length) - headerBytes) / sizeof(CampNet::UsageEntry);
  const size_t count = min(static_cast<size_t>(packet.entryCount),
                           min(carried, static_cast<size_t>(CampNet::USAGE_ENTRIES_PER_PACKET)));
  Peer& peer = peers_[packet.header.stationId];
  peer.lastUsageMs = millis();
  for (size_t i = 0; i < count; ++i) {
    const CampNet::UsageEntry& entry = packet.entries[i];
    if (entry.uidLen == 0 || entry.uidLen > CampNet::UID_BYTES) continue;
    char hex[CampNet::UID_BYTES * 2 + 1];
    CampNet::uidToHex(entry.uid, entry.uidLen, hex);
    ledger_.upsert(packet.header.stationId, packet.header.role, hex, entry.gallons,
                   entry.sessions);
  }
}

void CampNetLink::resetStaging() {
  stagingStation_ = 0;
  stagingVersion_ = 0;
  stagingChunkCount_ = 0;
  stagingTotal_ = 0;
  stagingReceivedMask_ = 0;
  stagingStartMs_ = 0;
}

void CampNetLink::handleMembers(const CampNet::MembersPacket& packet, int length) {
  Peer& peer = peers_[packet.header.stationId];
  peer.membersVersion = packet.version;
  // Do not noteRemoteVersion() here: applyRemoteSnapshot() records the version
  // once the full snapshot is accepted. Recording it per chunk let a station
  // that missed a chunk (or booted on a blank card) enrol at version+1 with a
  // stale/empty table and wipe the registry camp-wide.
  // Older than what we hold: nothing to learn (they will adopt ours).
  if (packet.version < members_.version()) return;
  if (packet.chunkCount == 0 || packet.chunkCount > 32 ||
      packet.chunkIndex >= packet.chunkCount ||
      packet.totalMembers > MemberRegistry::MAX_MEMBERS) return;

  const bool sameSnapshot = stagingStation_ == packet.header.stationId &&
                            stagingVersion_ == packet.version &&
                            stagingChunkCount_ == packet.chunkCount &&
                            stagingTotal_ == packet.totalMembers;
  if (!sameSnapshot) {
    // Finish the snapshot in progress unless this one is strictly newer or the
    // current one has stalled; otherwise two stations bursting equal-version
    // copies at the same time would keep restarting each other.
    const bool stalled = millis() - stagingStartMs_ >= Config::NET_STAGING_TIMEOUT_MS;
    if (stagingStation_ != 0 && packet.version <= stagingVersion_ && !stalled) return;
    resetStaging();
    stagingStation_ = packet.header.stationId;
    stagingVersion_ = packet.version;
    stagingChunkCount_ = packet.chunkCount;
    stagingTotal_ = packet.totalMembers;
    stagingStartMs_ = millis();
  }

  const size_t headerBytes = sizeof(CampNet::MembersPacket) -
                             sizeof(CampNet::MemberEntry) * CampNet::MEMBER_ENTRIES_PER_PACKET;
  const size_t carried = (static_cast<size_t>(length) - headerBytes) / sizeof(CampNet::MemberEntry);
  const size_t count = min(static_cast<size_t>(packet.entryCount),
                           min(carried, static_cast<size_t>(CampNet::MEMBER_ENTRIES_PER_PACKET)));
  const size_t base = static_cast<size_t>(packet.chunkIndex) * CampNet::MEMBER_ENTRIES_PER_PACKET;
  for (size_t i = 0; i < count && base + i < MemberRegistry::MAX_MEMBERS; ++i) {
    staging_[base + i] = packet.entries[i];
  }
  stagingReceivedMask_ |= static_cast<uint32_t>(1UL << packet.chunkIndex);

  const uint32_t complete = packet.chunkCount == 32
                                ? UINT32_MAX
                                : static_cast<uint32_t>((1UL << packet.chunkCount) - 1UL);
  if (stagingReceivedMask_ != complete) return;

  const uint8_t station = stagingStation_;
  const uint32_t version = stagingVersion_;
  const size_t total = stagingTotal_;
  resetStaging();
  if (members_.applyRemoteSnapshot(station, version, staging_, total)) {
    ++remoteChanges_;
    membersDirty_ = true;  // relay the adopted table for anyone out of range
    Serial.printf("[NET] members updated from station %u: version=%lu count=%u\n", station,
                  static_cast<unsigned long>(members_.version()),
                  static_cast<unsigned>(members_.count()));
  }
}

void CampNetLink::handleLimits(const CampNet::LimitsPacket& packet) {
  Peer& peer = peers_[packet.header.stationId];
  peer.limitsVersion = packet.version;
  SettingsStore::RoleLimits limits[CampNet::ROLE_COUNT];
  for (uint8_t role = 0; role < CampNet::ROLE_COUNT; ++role) {
    limits[role].gallons = packet.gallons[role];
    limits[role].minutes = packet.minutes[role];
  }
  if (settings_.applyRemoteLimits(packet.version, packet.header.stationId, limits)) {
    ++remoteChanges_;
    limitsDirty_ = true;
    Serial.printf("[NET] limits updated from station %u: version=%lu shower=%.1fgal/%umin water=%.1fgal/%umin rv=%.1fgal/%umin\n",
                  packet.header.stationId, static_cast<unsigned long>(packet.version),
                  limits[0].gallons, limits[0].minutes, limits[1].gallons,
                  limits[1].minutes, limits[2].gallons, limits[2].minutes);
  }
}

void CampNetLink::handleAuth(const CampNet::AuthPacket& packet) {
  if (!packetAuthentic(packet, AUTH_SIGNED_FROM)) {
    ++rxRejected_;
    return;
  }
  Peer& peer = peers_[packet.header.stationId];
  peer.authVersion = packet.version;
  if (settings_.applyRemoteAuth(packet.version, packet.header.stationId, packet.salt,
                                packet.hash)) {
    ++remoteChanges_;
    authDirty_ = true;  // relay for anyone out of range of the editor
    Serial.printf("[NET] admin password updated from station %u (version %lu)\n",
                  packet.header.stationId, static_cast<unsigned long>(packet.version));
  }
}

void CampNetLink::handleTelemetry(const CampNet::TelemetryPacket& packet) {
  RemoteTelemetry& slot = remoteTelemetry_[packet.header.stationId];
  slot.packet = packet;
  // Never trust the wire to terminate strings.
  slot.packet.activeName[sizeof(slot.packet.activeName) - 1] = '\0';
  slot.packet.pendingName[sizeof(slot.packet.pendingName) - 1] = '\0';
  slot.packet.speaker[sizeof(slot.packet.speaker) - 1] = '\0';
  slot.packet.playback[sizeof(slot.packet.playback) - 1] = '\0';
  slot.packet.calibrationMessage[sizeof(slot.packet.calibrationMessage) - 1] = '\0';
  slot.packet.message[sizeof(slot.packet.message) - 1] = '\0';
  slot.valid = true;
  slot.receivedMs = millis();
}

void CampNetLink::handleRecent(const CampNet::RecentPacket& packet, int length) {
  const size_t carried = (static_cast<size_t>(length) - recentPacketLength(0)) /
                         sizeof(CampNet::RecentEntry);
  const uint8_t count = static_cast<uint8_t>(
      min(static_cast<size_t>(packet.count),
          min(carried, static_cast<size_t>(CampNet::RECENT_ENTRIES_PER_PACKET))));
  RemoteRecent& slot = remoteRecent_[packet.header.stationId];
  slot.packet = packet;
  slot.packet.count = count;
  slot.valid = true;
  slot.receivedMs = millis();
}

// ---- Commands ----

bool CampNetLink::rememberCommandId(uint8_t sender, uint32_t nonce, uint32_t now) {
  for (RecentCommandId& id : recentIds_) {
    if (id.nonce == nonce && id.sender == sender &&
        now - id.seenMs < Config::NET_COMMAND_REPLAY_WINDOW_MS) {
      return false;  // already seen
    }
  }
  RecentCommandId& slot = recentIds_[recentIdNext_];
  recentIdNext_ = (recentIdNext_ + 1) % RECENT_COMMAND_IDS;
  slot.sender = sender;
  slot.nonce = nonce;
  slot.seenMs = now;
  return true;
}

CampNetLink::CachedAck* CampNetLink::findCachedAck(uint8_t target, uint32_t nonce) {
  for (CachedAck& ack : ackCache_) {
    if (ack.valid && ack.packet.target == target && ack.packet.nonce == nonce) return &ack;
  }
  return nullptr;
}

void CampNetLink::handleCommand(const CampNet::CommandPacket& packet) {
  if (packet.target != Config::STATION_ID_VALUE) return;  // not for us; not an error
  if (packet.argLen > CampNet::COMMAND_ARG_BYTES || packet.nonce == 0 ||
      !packetAuthentic(packet, COMMAND_SIGNED_FROM)) {
    ++rxRejected_;
    Serial.printf("[NET] rejected command from station %u (bad mac or malformed)\n",
                  packet.header.stationId);
    return;
  }
  const uint32_t now = millis();
  const uint8_t sender = packet.header.stationId;
  if (!rememberCommandId(sender, packet.nonce, now)) {
    // Retry of something we already handled (or are still handling). If we
    // already answered, the sender missed the ACK: repeat it.
    CachedAck* cached = findCachedAck(sender, packet.nonce);
    if (cached != nullptr) enqueue(&cached->packet, sizeof(cached->packet));
    return;
  }
  const size_t next = (incomingHead_ + 1) % INCOMING_COMMANDS;
  if (next == incomingTail_) {
    ++rxRejected_;
    Serial.printf("[NET] dropped command from station %u: queue full\n", sender);
    return;
  }
  IncomingCommand& command = incoming_[incomingHead_];
  command.fromStation = sender;
  command.action = packet.action;
  command.argLen = packet.argLen;
  memset(command.args, 0, sizeof(command.args));
  memcpy(command.args, packet.args, packet.argLen);
  command.nonce = packet.nonce;
  incomingHead_ = next;
  Serial.printf("[NET] command %u from station %u (nonce %08lx)\n", packet.action, sender,
                static_cast<unsigned long>(packet.nonce));
}

bool CampNetLink::takeIncomingCommand(IncomingCommand& out) {
  if (incomingTail_ == incomingHead_) return false;
  out = incoming_[incomingTail_];
  incomingTail_ = (incomingTail_ + 1) % INCOMING_COMMANDS;
  return true;
}

void CampNetLink::respondToCommand(const IncomingCommand& command, uint8_t status,
                                   const char* message) {
  if (command.fromStation == 0 || command.fromStation > CampNet::MAX_STATIONS ||
      command.nonce == 0) return;
  // Reuse the slot for this nonce if we are answering twice, else the oldest.
  CachedAck* slot = findCachedAck(command.fromStation, command.nonce);
  if (slot == nullptr) {
    slot = &ackCache_[0];
    for (CachedAck& ack : ackCache_) {
      if (!ack.valid) {
        slot = &ack;
        break;
      }
      if (static_cast<int32_t>(ack.sentMs - slot->sentMs) < 0) slot = &ack;
    }
  }
  CampNet::AckPacket& packet = slot->packet;
  memset(&packet, 0, sizeof(packet));
  fillHeader(packet.header, CampNet::PKT_ACK);
  packet.target = command.fromStation;
  packet.status = status;
  packet.nonce = command.nonce;
  strlcpy(packet.message, message != nullptr ? message : "", sizeof(packet.message));
  signPacket(packet, ACK_SIGNED_FROM);
  slot->valid = true;
  slot->repeatPending = true;
  slot->sentMs = millis();
  enqueue(&packet, sizeof(packet));
}

uint32_t CampNetLink::sendCommand(uint8_t target, uint8_t action, const uint8_t* args,
                                  uint8_t argLen) {
  if (!ready_ || target == 0 || target > CampNet::MAX_STATIONS ||
      target == Config::STATION_ID_VALUE || argLen > CampNet::COMMAND_ARG_BYTES ||
      (argLen > 0 && args == nullptr)) {
    return 0;
  }
  const uint32_t now = millis();
  OutstandingCommand* slot = nullptr;
  OutstandingCommand* oldestDone = nullptr;
  for (OutstandingCommand& entry : outstanding_) {
    if (entry.nonce == 0) {
      slot = &entry;
      break;
    }
    if (entry.state != CommandResult::State::Pending &&
        (oldestDone == nullptr || static_cast<int32_t>(entry.doneMs - oldestDone->doneMs) < 0)) {
      oldestDone = &entry;
    }
  }
  if (slot == nullptr) slot = oldestDone;
  if (slot == nullptr) return 0;  // four commands in flight already

  uint32_t nonce = 0;
  while (nonce == 0) nonce = esp_random();
  *slot = OutstandingCommand();
  slot->nonce = nonce;
  slot->target = target;
  slot->state = CommandResult::State::Pending;
  CampNet::CommandPacket& packet = slot->packet;
  memset(&packet, 0, sizeof(packet));
  fillHeader(packet.header, CampNet::PKT_COMMAND);
  packet.target = target;
  packet.action = action;
  packet.argLen = argLen;
  packet.nonce = nonce;
  if (argLen > 0) memcpy(packet.args, args, argLen);
  signPacket(packet, COMMAND_SIGNED_FROM);
  slot->attempts = 1;
  slot->lastSendMs = now;
  enqueue(&packet, sizeof(packet));
  Serial.printf("[NET] command %u to station %u (nonce %08lx)\n", action, target,
                static_cast<unsigned long>(nonce));
  return nonce;
}

CampNetLink::CommandResult CampNetLink::commandResult(uint32_t nonce) const {
  CommandResult result;
  if (nonce == 0) return result;
  for (const OutstandingCommand& entry : outstanding_) {
    if (entry.nonce != nonce) continue;
    result.state = entry.state;
    result.status = entry.status;
    strlcpy(result.message, entry.message, sizeof(result.message));
    return result;
  }
  return result;
}

void CampNetLink::handleAck(const CampNet::AckPacket& packet) {
  if (packet.target != Config::STATION_ID_VALUE) return;
  if (!packetAuthentic(packet, ACK_SIGNED_FROM)) {
    ++rxRejected_;
    return;
  }
  for (OutstandingCommand& entry : outstanding_) {
    if (entry.nonce != packet.nonce || entry.target != packet.header.stationId) continue;
    if (entry.state != CommandResult::State::Pending) return;  // duplicate ACK
    entry.state = CommandResult::State::Done;
    entry.status = packet.status;
    entry.doneMs = millis();
    strlcpy(entry.message, packet.message, sizeof(entry.message));
    Serial.printf("[NET] ack from station %u (nonce %08lx): status=%u %s\n",
                  packet.header.stationId, static_cast<unsigned long>(packet.nonce),
                  packet.status, entry.message);
    return;
  }
}

void CampNetLink::serviceCommands(uint32_t now) {
  for (OutstandingCommand& entry : outstanding_) {
    if (entry.nonce == 0) continue;
    if (entry.state == CommandResult::State::Pending) {
      if (now - entry.lastSendMs < Config::NET_COMMAND_RETRY_MS) continue;
      if (entry.attempts >= Config::NET_COMMAND_ATTEMPTS) {
        entry.state = CommandResult::State::Timeout;
        entry.doneMs = now;
        strlcpy(entry.message, "No response from station", sizeof(entry.message));
        Serial.printf("[NET] command to station %u timed out (nonce %08lx)\n", entry.target,
                      static_cast<unsigned long>(entry.nonce));
        continue;
      }
      ++entry.attempts;
      entry.lastSendMs = now;
      enqueue(&entry.packet, sizeof(entry.packet));
      continue;
    }
    if (now - entry.doneMs >= Config::NET_COMMAND_RESULT_TTL_MS) entry.nonce = 0;
  }
  for (CachedAck& ack : ackCache_) {
    if (!ack.valid || !ack.repeatPending) continue;
    if (now - ack.sentMs < Config::NET_ACK_REPEAT_MS) continue;
    ack.repeatPending = false;
    enqueue(&ack.packet, sizeof(ack.packet));
  }
}

// ---- Outgoing snapshots ----

void CampNetLink::setDoorState(uint8_t doorState, bool sessionActive) {
  if (doorState == doorState_ && sessionActive == sessionActive_) return;
  doorState_ = doorState;
  sessionActive_ = sessionActive;
  statusDirty_ = true;
}

void CampNetLink::setLocalTelemetry(const CampNet::TelemetryPacket& telemetry) {
  localTelemetry_ = telemetry;
  haveLocalTelemetry_ = true;
  CampNet::TelemetryPacket normalized = telemetry;
  normalizeTelemetry(normalized);
  if (memcmp(&normalized, &lastTelemetrySent_, sizeof(normalized)) != 0) telemetryDirty_ = true;
}

void CampNetLink::setLocalRecent(const CampNet::RecentPacket& recent) {
  localRecent_ = recent;
  if (localRecent_.count > CampNet::RECENT_ENTRIES_PER_PACKET) {
    localRecent_.count = CampNet::RECENT_ENTRIES_PER_PACKET;
  }
  haveLocalRecent_ = true;
  CampNet::RecentPacket normalized = localRecent_;
  normalizeRecent(normalized);
  if (memcmp(&normalized, &lastRecentSent_, sizeof(normalized)) != 0) recentDirty_ = true;
}

const CampNetLink::RemoteTelemetry& CampNetLink::telemetry(uint8_t stationId) const {
  static RemoteTelemetry empty;
  if (remoteTelemetry_ == nullptr || stationId == 0 || stationId > CampNet::MAX_STATIONS) return empty;
  return remoteTelemetry_[stationId];
}

const CampNetLink::RemoteRecent& CampNetLink::recent(uint8_t stationId) const {
  static RemoteRecent empty;
  if (remoteRecent_ == nullptr || stationId == 0 || stationId > CampNet::MAX_STATIONS) return empty;
  return remoteRecent_[stationId];
}

void CampNetLink::sendStatus() {
  CampNet::StatusPacket packet = {};
  fillHeader(packet.header, CampNet::PKT_STATUS);
  packet.doorState = doorState_;
  packet.sessionActive = sessionActive_ ? 1 : 0;
  packet.uptimeS = millis() / 1000UL;
  enqueue(&packet, sizeof(packet));
}

void CampNetLink::sendTelemetry() {
  CampNet::TelemetryPacket packet = localTelemetry_;
  fillHeader(packet.header, CampNet::PKT_TELEMETRY);
  enqueue(&packet, sizeof(packet));
  lastTelemetrySent_ = packet;
  normalizeTelemetry(lastTelemetrySent_);
}

void CampNetLink::sendRecent() {
  CampNet::RecentPacket packet = localRecent_;
  fillHeader(packet.header, CampNet::PKT_RECENT);
  enqueue(&packet, recentPacketLength(packet.count));
  lastRecentSent_ = packet;
  normalizeRecent(lastRecentSent_);
}

uint32_t CampNetLink::usageFingerprint() const {
  // FNV-1a over the per-wristband totals: cheap, and any change flips it.
  uint32_t hash = 2166136261UL;
  auto mix = [&hash](const void* data, size_t length) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < length; ++i) {
      hash ^= bytes[i];
      hash *= 16777619UL;
    }
  };
  const size_t total = sessions_.totalCount();
  mix(&total, sizeof(total));
  for (size_t i = 0; i < total; ++i) {
    const char* uid = sessions_.totalUidAt(i);
    mix(uid, strlen(uid));
    const float gallons = sessions_.totalGallonsAt(i);
    const uint32_t count = sessions_.totalSessionsAt(i);
    mix(&gallons, sizeof(gallons));
    mix(&count, sizeof(count));
  }
  return hash;
}

void CampNetLink::sendUsageSnapshot() {
  const size_t total = sessions_.totalCount();
  const size_t chunkCount =
      max(static_cast<size_t>(1),
          (total + CampNet::USAGE_ENTRIES_PER_PACKET - 1) / CampNet::USAGE_ENTRIES_PER_PACKET);
  ++usageSnapshotVersion_;
  size_t index = 0;
  for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
    CampNet::UsagePacket packet = {};
    fillHeader(packet.header, CampNet::PKT_USAGE);
    packet.chunkIndex = static_cast<uint8_t>(chunk);
    packet.chunkCount = static_cast<uint8_t>(chunkCount);
    packet.snapshotVersion = usageSnapshotVersion_;
    uint8_t count = 0;
    while (index < total && count < CampNet::USAGE_ENTRIES_PER_PACKET) {
      CampNet::UsageEntry& entry = packet.entries[count];
      entry.uidLen = CampNet::uidFromHex(sessions_.totalUidAt(index), entry.uid);
      entry.gallons = sessions_.totalGallonsAt(index);
      const uint32_t sessions = sessions_.totalSessionsAt(index);
      entry.sessions = static_cast<uint16_t>(min(sessions, static_cast<uint32_t>(0xFFFF)));
      ++index;
      if (entry.uidLen > 0) ++count;
    }
    packet.entryCount = count;
    const size_t length = sizeof(packet) -
                          sizeof(CampNet::UsageEntry) * (CampNet::USAGE_ENTRIES_PER_PACKET - count);
    enqueue(&packet, length);
  }
  lastUsageFingerprint_ = usageFingerprint();
}

void CampNetLink::sendMembersSnapshot() {
  CampNet::MemberEntry* entries = snapshot_;
  const size_t total = members_.fillSnapshot(entries, MemberRegistry::MAX_MEMBERS);
  const size_t chunkCount =
      max(static_cast<size_t>(1),
          (total + CampNet::MEMBER_ENTRIES_PER_PACKET - 1) / CampNet::MEMBER_ENTRIES_PER_PACKET);
  for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
    CampNet::MembersPacket packet = {};
    fillHeader(packet.header, CampNet::PKT_MEMBERS);
    packet.chunkIndex = static_cast<uint8_t>(chunk);
    packet.chunkCount = static_cast<uint8_t>(chunkCount);
    packet.totalMembers = static_cast<uint8_t>(total);
    packet.version = members_.version();
    const size_t base = chunk * CampNet::MEMBER_ENTRIES_PER_PACKET;
    uint8_t count = 0;
    while (base + count < total && count < CampNet::MEMBER_ENTRIES_PER_PACKET) {
      packet.entries[count] = entries[base + count];
      ++count;
    }
    packet.entryCount = count;
    const size_t length = sizeof(packet) -
                          sizeof(CampNet::MemberEntry) * (CampNet::MEMBER_ENTRIES_PER_PACKET - count);
    enqueue(&packet, length);
  }
}

void CampNetLink::sendLimits() {
  CampNet::LimitsPacket packet = {};
  fillHeader(packet.header, CampNet::PKT_LIMITS);
  packet.version = settings_.limitsVersion();
  for (uint8_t role = 0; role < CampNet::ROLE_COUNT; ++role) {
    packet.gallons[role] = settings_.roleLimits(role).gallons;
    packet.minutes[role] = settings_.roleLimits(role).minutes;
  }
  enqueue(&packet, sizeof(packet));
}

void CampNetLink::sendAuth() {
  CampNet::AuthPacket packet = {};
  fillHeader(packet.header, CampNet::PKT_AUTH);
  packet.version = settings_.authVersion();
  memcpy(packet.salt, settings_.salt(), sizeof(packet.salt));
  memcpy(packet.hash, settings_.passwordHash(), sizeof(packet.hash));
  signPacket(packet, AUTH_SIGNED_FROM);
  enqueue(&packet, sizeof(packet));
}

bool CampNetLink::peersInSync(uint32_t Peer::*field, uint32_t version) const {
  for (uint8_t id = 1; id <= CampNet::MAX_STATIONS; ++id) {
    if (!peerOnline(id)) continue;
    if (peers_[id].*field != version) return false;
  }
  return true;
}

void CampNetLink::handle() {
  if (!ready_) return;
  drainRx();
  const uint32_t now = millis();

  if (stagingStation_ != 0 && now - stagingStartMs_ > Config::NET_STAGING_TIMEOUT_MS) {
    resetStaging();
  }

  if (statusDirty_ || now - lastStatusMs_ >= Config::NET_STATUS_INTERVAL_MS) {
    statusDirty_ = false;
    lastStatusMs_ = now;
    sendStatus();
  }

  serviceCommands(now);

  // Give the radio and peers a moment after boot before the bigger snapshots.
  if (now - bootMs_ >= Config::NET_BOOT_ANNOUNCE_DELAY_MS) {
    // Periodic resends are skipped while every online peer already has what
    // we would send; dirty flags (local edits, a peer appearing) always win.
    if (now - lastUsageMs_ >= Config::NET_USAGE_INTERVAL_MS) {
      const bool changed = usageFingerprint() != lastUsageFingerprint_;
      const bool refresh = now - lastUsageRefreshMs_ >= Config::NET_USAGE_REFRESH_MS;
      if (changed || refresh) usageDirty_ = true;
      if (refresh) lastUsageRefreshMs_ = now;
      lastUsageMs_ = now;
    }
    if (usageDirty_) {
      usageDirty_ = false;
      lastUsageMs_ = now;
      sendUsageSnapshot();
    }
    if (now - lastMembersMs_ >= Config::NET_MEMBERS_INTERVAL_MS) {
      lastMembersMs_ = now;
      if (!peersInSync(&Peer::membersVersion, members_.version())) membersDirty_ = true;
    }
    if (membersDirty_) {
      membersDirty_ = false;
      lastMembersMs_ = now;
      sendMembersSnapshot();
    }
    if (now - lastLimitsMs_ >= Config::NET_LIMITS_INTERVAL_MS) {
      lastLimitsMs_ = now;
      if (!peersInSync(&Peer::limitsVersion, settings_.limitsVersion())) limitsDirty_ = true;
    }
    if (limitsDirty_) {
      limitsDirty_ = false;
      lastLimitsMs_ = now;
      sendLimits();
    }
    if (now - lastAuthMs_ >= Config::NET_AUTH_INTERVAL_MS) {
      lastAuthMs_ = now;
      if (!peersInSync(&Peer::authVersion, settings_.authVersion())) authDirty_ = true;
    }
    if (!Config::ADMIN_PAGE_PASSWORD) authDirty_ = false;  // nothing to sync
    if (authDirty_) {
      authDirty_ = false;
      lastAuthMs_ = now;
      sendAuth();
    }
    if (haveLocalTelemetry_ &&
        ((telemetryDirty_ && now - lastTelemetryMs_ >= Config::NET_TELEMETRY_MIN_INTERVAL_MS) ||
         now - lastTelemetryMs_ >= Config::NET_TELEMETRY_INTERVAL_MS)) {
      telemetryDirty_ = false;
      lastTelemetryMs_ = now;
      sendTelemetry();
    }
    if (haveLocalRecent_ &&
        (recentDirty_ || now - lastRecentMs_ >= Config::NET_RECENT_INTERVAL_MS)) {
      recentDirty_ = false;
      lastRecentMs_ = now;
      sendRecent();
    }
  }

  pumpTx();
}

size_t CampNetLink::peerCount() const {
  size_t count = 0;
  for (uint8_t id = 1; id <= CampNet::MAX_STATIONS; ++id) {
    if (peerOnline(id)) ++count;
  }
  return count;
}

bool CampNetLink::peerOnline(uint8_t stationId) const {
  if (stationId == 0 || stationId > CampNet::MAX_STATIONS) return false;
  const Peer& peer = peers_[stationId];
  return peer.seen && millis() - peer.lastSeenMs < Config::NET_PEER_TIMEOUT_MS;
}

const CampNetLink::Peer& CampNetLink::peer(uint8_t stationId) const {
  static Peer empty;
  return stationId <= CampNet::MAX_STATIONS ? peers_[stationId] : empty;
}
