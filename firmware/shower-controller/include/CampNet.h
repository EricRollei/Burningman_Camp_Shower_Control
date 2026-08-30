#pragma once

#include <Arduino.h>

#include "CampNetEspNow.h"
#include "CampNetProtocol.h"
#include "MemberRegistry.h"
#include "SessionStorage.h"
#include "SettingsStore.h"
#include "UsageLedger.h"

// ESP-NOW broadcast link between every station and door sign. Owns the Wi-Fi
// radio: it brings up this station's admin soft-AP pinned to CampNet::CHANNEL
// and layers ESP-NOW on the AP interface. Everything it receives is data
// (usage totals, member registry, limits, admin password, telemetry) or an
// authenticated admin command that the AdminServer executes through the same
// code paths as a local admin click; nothing on the network can drive a
// relay directly.
class CampNetLink {
 public:
  struct Peer {
    bool seen = false;
    uint8_t role = 0;
    uint8_t doorState = 0;
    bool sessionActive = false;
    uint32_t lastSeenMs = 0;
    uint32_t uptimeS = 0;
    uint32_t membersVersion = 0;
    uint32_t limitsVersion = 0;
    uint32_t authVersion = 0;
    uint32_t lastUsageMs = 0;
  };

  CampNetLink(MemberRegistry& members, const SessionStorage& sessions,
          SettingsStore& settings, UsageLedger& ledger);

  bool begin();
  void handle();
  bool ready() const { return ready_; }
  String address() const;

  void setDoorState(uint8_t doorState, bool sessionActive);
  void markUsageDirty() { usageDirty_ = true; }
  void markMembersDirty() { membersDirty_ = true; }
  void markLimitsDirty() { limitsDirty_ = true; }
  void markAuthDirty() { authDirty_ = true; }

  // ---- Single admin page: telemetry, recent sessions, remote commands ----
  // The AdminServer fills a TelemetryPacket / RecentPacket for this station
  // every loop; CampNet stamps the header, broadcasts on a timer and
  // immediately when the content (ignoring uptime/heap counters) changes.
  void setLocalTelemetry(const CampNet::TelemetryPacket& telemetry);
  void setLocalRecent(const CampNet::RecentPacket& recent);

  struct RemoteTelemetry {
    bool valid = false;
    uint32_t receivedMs = 0;
    CampNet::TelemetryPacket packet;
  };
  struct RemoteRecent {
    bool valid = false;
    uint32_t receivedMs = 0;
    CampNet::RecentPacket packet;
  };
  const RemoteTelemetry& telemetry(uint8_t stationId) const;
  const RemoteRecent& recent(uint8_t stationId) const;

  // Outgoing command to another station. Returns the request nonce (0 if the
  // link is down or the queue is full). CampNet retries until an ACK arrives
  // or it times out; poll commandResult(nonce) from the HTTP layer.
  struct CommandResult {
    enum class State : uint8_t { Unknown, Pending, Done, Timeout };
    State state = State::Unknown;
    uint8_t status = CampNet::ACK_REJECTED;  // AckStatus once Done
    char message[48] = {0};
  };
  uint32_t sendCommand(uint8_t target, uint8_t action, const uint8_t* args, uint8_t argLen);
  CommandResult commandResult(uint32_t nonce) const;

  // Incoming, already-authenticated and de-duplicated command addressed to
  // this station. The AdminServer executes it and must answer with
  // respondToCommand() (that sends the ACK).
  struct IncomingCommand {
    uint8_t fromStation = 0;
    uint8_t action = 0;
    uint8_t argLen = 0;
    uint8_t args[CampNet::COMMAND_ARG_BYTES] = {0};
    uint32_t nonce = 0;
  };
  bool takeIncomingCommand(IncomingCommand& out);
  void respondToCommand(const IncomingCommand& command, uint8_t status, const char* message);

  size_t peerCount() const;
  bool peerOnline(uint8_t stationId) const;
  const Peer& peer(uint8_t stationId) const;
  // Increments whenever a remote members, limits or password update was adopted.
  uint32_t remoteChangeCount() const { return remoteChanges_; }
  uint32_t rxPackets() const { return rxPackets_; }
  uint32_t rxDropped() const { return rxDropped_; }
  // COMMAND/ACK/AUTH frames refused: bad mac, wrong target, malformed, or
  // the incoming command queue was full.
  uint32_t rxRejected() const { return rxRejected_; }
  uint32_t txPackets() const { return txPackets_; }
  uint32_t txFailures() const { return txFailures_; }

 private:
  struct Frame {
    uint8_t length = 0;
    uint8_t data[CampNet::MAX_PAYLOAD];
  };
  static constexpr size_t RX_RING = 12;
  static constexpr size_t TX_RING = 24;
  static constexpr uint32_t TX_TIMEOUT_MS = 50;
  static constexpr size_t OUTSTANDING_COMMANDS = 4;
  static constexpr size_t INCOMING_COMMANDS = 4;
  static constexpr size_t RECENT_COMMAND_IDS = 16;
  static constexpr size_t ACK_CACHE = 4;

  struct OutstandingCommand {
    uint32_t nonce = 0;  // 0 = free slot
    uint8_t target = 0;
    uint8_t attempts = 0;
    CommandResult::State state = CommandResult::State::Unknown;
    uint8_t status = CampNet::ACK_REJECTED;
    uint32_t lastSendMs = 0;
    uint32_t doneMs = 0;
    char message[48] = {0};
    CampNet::CommandPacket packet;
  };
  struct RecentCommandId {
    uint8_t sender = 0;
    uint32_t nonce = 0;
    uint32_t seenMs = 0;
  };
  struct CachedAck {
    bool valid = false;
    bool repeatPending = false;
    uint32_t sentMs = 0;
    CampNet::AckPacket packet;
  };

  static CampNetLink* instance_;
  static void onReceive(CAMPNET_RECV_CB_PARAMS);
  static void onSent(CAMPNET_SEND_CB_PARAMS);

  void fillHeader(CampNet::Header& header, uint8_t type);
  bool enqueue(const void* data, size_t length);
  void pumpTx();
  void drainRx();
  void dispatch(const uint8_t* data, int length);
  void sendStatus();
  void sendUsageSnapshot();
  void sendMembersSnapshot();
  void sendLimits();
  void sendAuth();
  void sendTelemetry();
  void sendRecent();
  void handleUsage(const CampNet::UsagePacket& packet, int length);
  void handleMembers(const CampNet::MembersPacket& packet, int length);
  void handleLimits(const CampNet::LimitsPacket& packet);
  void handleAuth(const CampNet::AuthPacket& packet);
  void handleTelemetry(const CampNet::TelemetryPacket& packet);
  void handleRecent(const CampNet::RecentPacket& packet, int length);
  void handleCommand(const CampNet::CommandPacket& packet);
  void handleAck(const CampNet::AckPacket& packet);
  void serviceCommands(uint32_t now);
  bool rememberCommandId(uint8_t sender, uint32_t nonce, uint32_t now);
  CachedAck* findCachedAck(uint8_t target, uint32_t nonce);
  // True when every online peer has reported `version` for the given field,
  // so a periodic resend would carry nothing new.
  bool peersInSync(uint32_t Peer::*field, uint32_t version) const;
  uint32_t usageFingerprint() const;
  void resetStaging();

  MemberRegistry& members_;
  const SessionStorage& sessions_;
  SettingsStore& settings_;
  UsageLedger& ledger_;

  bool ready_ = false;
  bool apReady_ = false;
  uint16_t seq_ = 0;
  uint8_t doorState_ = CampNet::DOOR_UNAVAILABLE;
  bool sessionActive_ = false;
  bool statusDirty_ = true;
  bool usageDirty_ = true;
  bool membersDirty_ = true;
  bool limitsDirty_ = true;
  bool authDirty_ = true;
  bool telemetryDirty_ = false;
  bool recentDirty_ = false;
  bool haveLocalTelemetry_ = false;
  bool haveLocalRecent_ = false;
  uint32_t bootMs_ = 0;
  uint32_t lastStatusMs_ = 0;
  uint32_t lastUsageMs_ = 0;
  uint32_t lastUsageRefreshMs_ = 0;
  uint32_t lastUsageFingerprint_ = 0;
  uint32_t lastMembersMs_ = 0;
  uint32_t lastLimitsMs_ = 0;
  uint32_t lastAuthMs_ = 0;
  uint32_t lastTelemetryMs_ = 0;
  uint32_t lastRecentMs_ = 0;
  uint32_t usageSnapshotVersion_ = 0;
  uint32_t remoteChanges_ = 0;
  uint32_t rxPackets_ = 0;
  uint32_t rxDropped_ = 0;
  uint32_t rxRejected_ = 0;
  uint32_t txPackets_ = 0;
  uint32_t txFailures_ = 0;
  uint32_t lastDuplicateIdLogMs_ = 0;

  Frame rx_[RX_RING];
  volatile size_t rxHead_ = 0;
  volatile size_t rxTail_ = 0;
  portMUX_TYPE rxMux_ = portMUX_INITIALIZER_UNLOCKED;
  // Large CPU-only buffers live in PSRAM (see PsramAlloc.h); the rx ring stays
  // internal because the ESP-NOW callback fills it from the Wi-Fi task.
  Frame* tx_ = nullptr;  // TX_RING
  size_t txHead_ = 0;
  size_t txTail_ = 0;
  volatile bool txInFlight_ = false;
  uint32_t txStartedMs_ = 0;

  Peer peers_[CampNet::MAX_STATIONS + 1];

  // Local telemetry / recent sessions as last handed to us, plus a normalized
  // copy of what was last broadcast for change detection.
  CampNet::TelemetryPacket localTelemetry_ = {};
  CampNet::TelemetryPacket lastTelemetrySent_ = {};
  CampNet::RecentPacket localRecent_ = {};
  CampNet::RecentPacket lastRecentSent_ = {};
  RemoteTelemetry* remoteTelemetry_ = nullptr;  // MAX_STATIONS + 1, PSRAM
  RemoteRecent* remoteRecent_ = nullptr;        // MAX_STATIONS + 1, PSRAM

  // Remote command bookkeeping. Everything here is touched only from handle()
  // and the public API on the main loop; the ESP-NOW callbacks never see it.
  OutstandingCommand outstanding_[OUTSTANDING_COMMANDS];
  IncomingCommand incoming_[INCOMING_COMMANDS];
  size_t incomingHead_ = 0;
  size_t incomingTail_ = 0;
  RecentCommandId recentIds_[RECENT_COMMAND_IDS];
  size_t recentIdNext_ = 0;
  CachedAck ackCache_[ACK_CACHE];

  // Chunk reassembly for one incoming member snapshot at a time.
  CampNet::MemberEntry* staging_ = nullptr;   // MAX_MEMBERS, PSRAM
  CampNet::MemberEntry* snapshot_ = nullptr;  // MAX_MEMBERS, PSRAM (outgoing)
  uint8_t stagingStation_ = 0;
  uint32_t stagingVersion_ = 0;
  uint8_t stagingChunkCount_ = 0;
  uint8_t stagingTotal_ = 0;
  uint16_t stagingReceivedMask_ = 0;
  uint32_t stagingStartMs_ = 0;
};
