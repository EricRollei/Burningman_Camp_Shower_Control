#include "CampNet.h"

#include <WiFi.h>

#include "Config.h"

CampNetLink* CampNetLink::instance_ = nullptr;

CampNetLink::CampNetLink(MemberRegistry& members, const SessionStorage& sessions,
                 SettingsStore& settings, UsageLedger& ledger)
    : members_(members), sessions_(sessions), settings_(settings), ledger_(ledger) {}

bool CampNetLink::begin() {
  if (ready_) return true;
  instance_ = this;
  bootMs_ = millis();

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
  Peer& peer = peers_[header.stationId];
  const bool firstSighting = !peer.seen;
  peer.seen = true;
  peer.role = header.role;
  peer.lastSeenMs = millis();
  if (firstSighting) {
    Serial.printf("[NET] peer %u (%s) online\n", header.stationId,
                  CampNet::roleName(header.role));
    // A newcomer (or a reboot) should hear our state right away.
    usageDirty_ = membersDirty_ = limitsDirty_ = true;
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
  members_.noteRemoteVersion(packet.version);
  // Older than what we hold: nothing to learn (they will adopt ours).
  if (packet.version < members_.version()) return;
  if (packet.chunkCount == 0 || packet.chunkCount > 16 ||
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
  stagingReceivedMask_ |= static_cast<uint16_t>(1U << packet.chunkIndex);

  const uint16_t complete = static_cast<uint16_t>((1U << packet.chunkCount) - 1U);
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

void CampNetLink::setDoorState(uint8_t doorState, bool sessionActive) {
  if (doorState == doorState_ && sessionActive == sessionActive_) return;
  doorState_ = doorState;
  sessionActive_ = sessionActive;
  statusDirty_ = true;
}

void CampNetLink::sendStatus() {
  CampNet::StatusPacket packet = {};
  fillHeader(packet.header, CampNet::PKT_STATUS);
  packet.doorState = doorState_;
  packet.sessionActive = sessionActive_ ? 1 : 0;
  packet.uptimeS = millis() / 1000UL;
  enqueue(&packet, sizeof(packet));
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
}

void CampNetLink::sendMembersSnapshot() {
  static CampNet::MemberEntry entries[MemberRegistry::MAX_MEMBERS];
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

  // Give the radio and peers a moment after boot before the bigger snapshots.
  if (now - bootMs_ >= Config::NET_BOOT_ANNOUNCE_DELAY_MS) {
    if (usageDirty_ || now - lastUsageMs_ >= Config::NET_USAGE_INTERVAL_MS) {
      usageDirty_ = false;
      lastUsageMs_ = now;
      sendUsageSnapshot();
    }
    if (membersDirty_ || now - lastMembersMs_ >= Config::NET_MEMBERS_INTERVAL_MS) {
      membersDirty_ = false;
      lastMembersMs_ = now;
      sendMembersSnapshot();
    }
    if (limitsDirty_ || now - lastLimitsMs_ >= Config::NET_LIMITS_INTERVAL_MS) {
      limitsDirty_ = false;
      lastLimitsMs_ = now;
      sendLimits();
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

// ---- Single admin page support: stubs, implemented by the transport work ----
void CampNetLink::setLocalTelemetry(const CampNet::TelemetryPacket& telemetry) { (void)telemetry; }
void CampNetLink::setLocalRecent(const CampNet::RecentPacket& recent) { (void)recent; }
const CampNetLink::RemoteTelemetry& CampNetLink::telemetry(uint8_t stationId) const {
  static RemoteTelemetry empty;
  (void)stationId;
  return empty;
}
const CampNetLink::RemoteRecent& CampNetLink::recent(uint8_t stationId) const {
  static RemoteRecent empty;
  (void)stationId;
  return empty;
}
uint32_t CampNetLink::sendCommand(uint8_t target, uint8_t action, const uint8_t* args, uint8_t argLen) {
  (void)target; (void)action; (void)args; (void)argLen;
  return 0;
}
CampNetLink::CommandResult CampNetLink::commandResult(uint32_t nonce) const {
  (void)nonce;
  return CommandResult();
}
bool CampNetLink::takeIncomingCommand(IncomingCommand& out) { (void)out; return false; }
void CampNetLink::respondToCommand(const IncomingCommand& command, uint8_t status, const char* message) {
  (void)command; (void)status; (void)message;
}
