// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 IllumiNocte
#include "NocteNetSacn.h"
#include <cstring>

namespace nocte { namespace net {
SacnSourceSelector::SacnSourceSelector() { std::memset(sources_, 0, sizeof(sources_)); }
void SacnSourceSelector::clear() { for (auto& source : sources_) source.active = false; }
bool SacnSourceSelector::expire(uint32_t now) {
  bool expired = false;
  for (auto& source : sources_) {
    if (source.active && uint32_t(now - source.lastMillis) >= SACN_SOURCE_TIMEOUT_MS) {
      source.active = false; ++diagnostics_.sourceTimeouts; expired = true;
    }
  }
  return expired;
}
uint8_t SacnSourceSelector::activeCount() const {
  uint8_t count = 0; for (const auto& source : sources_) if (source.active) ++count;
  return count;
}
uint8_t SacnSourceSelector::highestPriority() const {
  uint8_t priority = 0;
  for (const auto& source : sources_) if (source.active && source.priority > priority) priority = source.priority;
  return priority;
}
const SacnSource* SacnSourceSelector::selected() const {
  const SacnSource* best = nullptr;
  for (const auto& source : sources_) {
    if (source.active && (!best || source.priority > best->priority
        || (source.priority == best->priority && source.lastMillis > best->lastMillis))) best = &source;
  }
  return best;
}
uint8_t SacnSourceSelector::findSlot(const uint8_t* cid, uint32_t now) const {
  uint8_t oldest = 0; uint32_t oldestAge = 0;
  // Preserve the initial uNode policy, including first-free-slot selection.
  // An inactive earlier slot may precede a matching CID; see docs/sacn.md.
  for (uint8_t i = 0; i < 2; ++i) {
    const auto& source = sources_[i];
    if (source.active && std::memcmp(source.cid, cid, 16) == 0) return i;
    if (!source.active) return i;
    const uint32_t age = now - source.lastMillis;
    if (age >= oldestAge) { oldestAge = age; oldest = i; }
  }
  return oldest;
}
SacnSourceResult SacnSourceSelector::accept(const SacnData& data, uint32_t now) {
  if (!data.cid || data.length > SACN_CHANNEL_COUNT || (data.length && !data.slots)) return SacnSourceResult::Invalid;
  expire(now);
  SacnSource& source = sources_[findSlot(data.cid, now)];
  if (source.active && std::memcmp(source.cid, data.cid, 16) == 0
      && (data.sequence == source.sequence || uint8_t(data.sequence - source.sequence) >= 128)) {
    ++diagnostics_.sequenceDrops; return SacnSourceResult::SequenceDrop;
  }
  std::memcpy(source.cid, data.cid, 16); source.sequence = data.sequence;
  source.priority = data.priority; source.lastMillis = now; source.length = data.length;
  if (data.length) std::memcpy(source.frame, data.slots, data.length);
  if (data.length < SACN_CHANNEL_COUNT) std::memset(source.frame + data.length, 0, SACN_CHANNEL_COUNT - data.length);
  source.active = !data.terminated();
  if (data.terminated()) { ++diagnostics_.streamTerminated; return SacnSourceResult::Terminated; }
  if (data.priority < highestPriority()) { ++diagnostics_.priorityDrops; return SacnSourceResult::PriorityDrop; }
  return SacnSourceResult::Accepted;
}
}} // namespace nocte::net
