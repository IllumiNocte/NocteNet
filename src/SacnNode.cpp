// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 IllumiNocte
#include "NocteNetSacn.h"

namespace nocte { namespace net {
SacnNode::SacnNode(SacnTransport& transport) : transport_(transport) {}
void SacnNode::stop() {
  transport_.stop(); ready_ = false; discardRemaining_ = 0;
  if (joined_) {
    if (transport_.leaveMulticast(interface_, group_)) ++diagnostics_.multicastLeaves;
    else ++diagnostics_.multicastLeaveFailures;
  }
  joined_ = false; interface_ = IPv4(); group_ = IPv4();
}
bool SacnNode::begin(IPv4 interfaceAddress, uint16_t universe) {
  stop(); ++diagnostics_.socketRebinds;
  interface_ = interfaceAddress; universe_ = universe; group_ = sacnMulticastAddress(universe);
  if (interface_ != IPv4()) {
    joined_ = transport_.joinMulticast(interface_, group_);
    if (joined_) {
      ++diagnostics_.multicastJoins;
      ready_ = transport_.begin(SACN_PORT);
      if (!ready_) {
        // Retain uNode's unicast fallback, but always release a successful join.
        stop(); interface_ = interfaceAddress; group_ = sacnMulticastAddress(universe);
      }
    } else ++diagnostics_.multicastJoinFailures;
  }
  if (!ready_) ready_ = transport_.begin(SACN_PORT);
  return ready_;
}
void SacnNode::setDataCallback(DataCallback callback, void* context) {
  callback_ = callback; context_ = context;
}
void SacnNode::discard(size_t unread) {
  discardRemaining_ = unread && !transport_.discardPacket() ? unread : 0;
}
bool SacnNode::read() {
  if (!ready_) return false;
  if (discardRemaining_) {
    const size_t chunk = lesser(discardRemaining_, size_t(256));
    const int count = transport_.read(buffer_, chunk);
    if (count > 0) discardRemaining_ -= lesser(discardRemaining_, size_t(count));
    else discardRemaining_ = 0; // adapter exhausted the packet; never block/spin
    return true;
  }
  const int size = transport_.parsePacket();
  if (size <= 0) return false;
  if (size_t(size) > sizeof(buffer_)) {
    ++diagnostics_.malformedPackets; discard(size_t(size)); return true;
  }
  const int count = transport_.read(buffer_, size_t(size));
  if (count > 0) ++diagnostics_.udpPackets;
  // A partial datagram is never passed to a parser as if it were complete.
  if (count != size) {
    ++diagnostics_.malformedPackets;
    discard(count > 0 && count < size ? size_t(size - count) : size_t(size));
    return true;
  }
  SacnData data{};
  if (!decodeSacnData(buffer_, size_t(count), data)) ++diagnostics_.malformedPackets;
  else if (data.universe != universe_) {
    ++diagnostics_.wrongUniversePackets; diagnostics_.lastWrongUniverse = data.universe;
  } else if (callback_) callback_(context_, data);
  return true;
}
bool SacnNode::send(const SacnSender& sender, const uint8_t* slots, uint16_t length) {
  if (!ready_) return false;
  const size_t size = encodeSacnData(buffer_, sizeof(buffer_), sender, slots, length);
  if (!size || !transport_.beginMulticastPacket(interface_, sacnMulticastAddress(sender.universe), SACN_PORT)) return false;
  const size_t written = transport_.write(buffer_, size);
  const bool sent = transport_.endPacket();
  return sent && written == size;
}
}} // namespace nocte::net
