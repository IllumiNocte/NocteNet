// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 IllumiNocte
#pragma once

#include "NocteNetTransport.h"

namespace nocte { namespace net {

static const uint16_t SACN_PORT = 5568;
static const size_t SACN_HEADER_SIZE = 126;
static const size_t SACN_MAX_PACKET_SIZE = 638;
static const uint16_t SACN_CHANNEL_COUNT = 512;
static const uint32_t SACN_SOURCE_TIMEOUT_MS = 2500;

/** Borrowed view: valid until the receive buffer is reused. Start code is zero. */
struct SacnData {
  const uint8_t* cid;
  const uint8_t* sourceName; // 64 wire bytes, not necessarily NUL terminated
  const uint8_t* slots;
  uint16_t length;
  uint16_t universe;
  uint8_t priority;
  uint8_t sequence;
  uint8_t options;
  bool terminated() const { return (options & 0x40) != 0; }
};

/** Explicit sender identity; CID and name are owned by the application. */
struct SacnSender {
  uint8_t cid[16];
  const char* name;
  uint16_t universe;
  uint8_t priority;
  uint8_t sequence;
  uint8_t options;
};

/** Initial uNode-compatible zero-start-code data subset, not a full E1.31 validator. */
bool decodeSacnData(const uint8_t* packet, size_t length, SacnData& data);
/** Returns encoded length, or zero for invalid pointers, slot count or capacity. */
size_t encodeSacnData(uint8_t* packet, size_t capacity, const SacnSender& sender,
                      const uint8_t* slots, uint16_t length);
IPv4 sacnMulticastAddress(uint16_t universe);

/**
 * Explicit multicast capability, separate from the Art-Net UDP adapter.
 * Membership and multicast TX must refer to the supplied interface address.
 * The adapter owns driver-specific cleanup (including stale-interface fallback).
 */
class SacnTransport : public UdpTransport {
public:
  virtual bool joinMulticast(IPv4 interfaceAddress, IPv4 group) = 0;
  virtual bool leaveMulticast(IPv4 interfaceAddress, IPv4 group) = 0;
  virtual bool beginMulticastPacket(IPv4 interfaceAddress, IPv4 group, uint16_t port) = 0;
  /** Optional O(1) discard. False selects the portable bounded drain path. */
  virtual bool discardPacket() { return false; }
};

struct SacnDiagnostics {
  uint32_t udpPackets = 0;
  uint32_t malformedPackets = 0;
  uint32_t wrongUniversePackets = 0;
  uint16_t lastWrongUniverse = 0;
  uint32_t multicastJoins = 0;
  uint32_t multicastLeaves = 0;
  uint32_t multicastJoinFailures = 0;
  uint32_t multicastLeaveFailures = 0;
  uint32_t socketRebinds = 0;
};

/**
 * One universe on one externally owned socket/interface. No radio setup, retries,
 * DMX, source merge, failsafe or heap allocation. Callbacks are cooperative,
 * borrowed, and must not re-enter read/send/begin/stop. Do not copy an active node.
 */
class SacnNode {
public:
  typedef void (*DataCallback)(void* context, const SacnData& data);
  SacnNode(SacnTransport& transport);
  SacnNode(const SacnNode&) = delete;
  SacnNode& operator=(const SacnNode&) = delete;
  bool begin(IPv4 interfaceAddress, uint16_t universe);
  void stop();
  void setDataCallback(DataCallback callback, void* context = nullptr);
  /** Processes one datagram or at most one 256-byte discard chunk; false if idle. */
  bool read();
  bool send(const SacnSender& sender, const uint8_t* slots, uint16_t length);
  bool ready() const { return ready_; }
  bool multicastJoined() const { return joined_; }
  const SacnDiagnostics& diagnostics() const { return diagnostics_; }
private:
  void discard(size_t unread);
  SacnTransport& transport_;
  DataCallback callback_ = nullptr;
  void* context_ = nullptr;
  IPv4 interface_, group_;
  uint16_t universe_ = 1;
  bool ready_ = false, joined_ = false;
  size_t discardRemaining_ = 0;
  uint8_t buffer_[SACN_MAX_PACKET_SIZE];
  SacnDiagnostics diagnostics_;
};

struct SacnSource {
  uint8_t cid[16];
  uint8_t sequence;
  uint8_t priority;
  uint32_t lastMillis;
  uint16_t length;
  uint8_t frame[SACN_CHANNEL_COUNT];
  bool active;
};
struct SacnSourceDiagnostics {
  uint32_t sequenceDrops = 0;
  uint32_t priorityDrops = 0;
  uint32_t streamTerminated = 0;
  uint32_t sourceTimeouts = 0;
};
enum class SacnSourceResult : uint8_t { Accepted, SequenceDrop, PriorityDrop, Terminated, Invalid };

/**
 * Optional two-source uNode selection policy: highest priority, latest timestamp
 * at equal priority, oldest-slot replacement. NOT an HTP merge or full E1.31
 * receiver. Kept separate from SacnNode so applications may supply other policies.
 * Clock values are explicitly supplied by the caller; no platform dependency.
 */
class SacnSourceSelector {
public:
  SacnSourceSelector();
  void clear(); // clears source state, not cumulative diagnostics
  bool expire(uint32_t now);
  SacnSourceResult accept(const SacnData& data, uint32_t now);
  const SacnSource* selected() const;
  uint8_t activeCount() const;
  uint8_t highestPriority() const;
  const SacnSourceDiagnostics& diagnostics() const { return diagnostics_; }
private:
  uint8_t findSlot(const uint8_t* cid, uint32_t now) const;
  SacnSource sources_[2];
  SacnSourceDiagnostics diagnostics_;
};

}} // namespace nocte::net
