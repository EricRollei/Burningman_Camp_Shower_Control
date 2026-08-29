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
// (usage totals, member registry, limits); nothing on the network can drive a
// relay.
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

  size_t peerCount() const;
  bool peerOnline(uint8_t stationId) const;
  const Peer& peer(uint8_t stationId) const;
  // Increments whenever a remote members or limits update was adopted.
  uint32_t remoteChangeCount() const { return remoteChanges_; }
  uint32_t rxPackets() const { return rxPackets_; }
  uint32_t rxDropped() const { return rxDropped_; }
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
  void handleUsage(const CampNet::UsagePacket& packet, int length);
  void handleMembers(const CampNet::MembersPacket& packet, int length);
  void handleLimits(const CampNet::LimitsPacket& packet);
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
  uint32_t bootMs_ = 0;
  uint32_t lastStatusMs_ = 0;
  uint32_t lastUsageMs_ = 0;
  uint32_t lastMembersMs_ = 0;
  uint32_t lastLimitsMs_ = 0;
  uint32_t usageSnapshotVersion_ = 0;
  uint32_t remoteChanges_ = 0;
  uint32_t rxPackets_ = 0;
  uint32_t rxDropped_ = 0;
  uint32_t txPackets_ = 0;
  uint32_t txFailures_ = 0;
  uint32_t lastDuplicateIdLogMs_ = 0;

  Frame rx_[RX_RING];
  volatile size_t rxHead_ = 0;
  volatile size_t rxTail_ = 0;
  portMUX_TYPE rxMux_ = portMUX_INITIALIZER_UNLOCKED;
  Frame tx_[TX_RING];
  size_t txHead_ = 0;
  size_t txTail_ = 0;
  volatile bool txInFlight_ = false;
  uint32_t txStartedMs_ = 0;

  Peer peers_[CampNet::MAX_STATIONS + 1];

  // Chunk reassembly for one incoming member snapshot at a time.
  CampNet::MemberEntry staging_[MemberRegistry::MAX_MEMBERS];
  uint8_t stagingStation_ = 0;
  uint32_t stagingVersion_ = 0;
  uint8_t stagingChunkCount_ = 0;
  uint8_t stagingTotal_ = 0;
  uint16_t stagingReceivedMask_ = 0;
  uint32_t stagingStartMs_ = 0;
};
