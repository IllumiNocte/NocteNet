// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 IllumiNocte
#include "NocteNetSacn.h"
#include <cstring>

namespace nocte { namespace net {
namespace {
const uint8_t identifier[12] = {'A','S','C','-','E','1','.','1','7',0,0,0};
uint16_t get16(const uint8_t* p, size_t i) {
  return static_cast<uint16_t>((uint16_t(p[i]) << 8) | p[i + 1]);
}
uint32_t get32(const uint8_t* p, size_t i) {
  return (uint32_t(p[i]) << 24) | (uint32_t(p[i + 1]) << 16)
    | (uint32_t(p[i + 2]) << 8) | p[i + 3];
}
void put16(uint8_t* p, size_t i, uint16_t v) { p[i] = uint8_t(v >> 8); p[i + 1] = uint8_t(v); }
void put32(uint8_t* p, size_t i, uint32_t v) {
  p[i] = uint8_t(v >> 24); p[i + 1] = uint8_t(v >> 16);
  p[i + 2] = uint8_t(v >> 8); p[i + 3] = uint8_t(v);
}
}

IPv4 sacnMulticastAddress(uint16_t universe) {
  return IPv4(239, 255, uint8_t(universe >> 8), uint8_t(universe));
}
bool decodeSacnData(const uint8_t* p, size_t length, SacnData& data) {
  if (!p || length < SACN_HEADER_SIZE || length > SACN_MAX_PACKET_SIZE
      || get16(p, 0) != 0x10 || get16(p, 2) != 0
      || std::memcmp(p + 4, identifier, sizeof(identifier)) != 0
      || get32(p, 18) != 4 || get32(p, 40) != 2
      || p[117] != 2 || p[118] != 0xa1 || get16(p, 119) != 0
      || get16(p, 121) != 1 || p[125] != 0
      || (get16(p, 16) & 0xfff) < 110 || (get16(p, 38) & 0xfff) < 88
      || (get16(p, 115) & 0xfff) < 11) return false;
  const uint16_t count = get16(p, 123);
  if (count < 1 || size_t(125) + count > length) return false;
  data.cid = p + 22; data.sourceName = p + 44; data.slots = p + 126;
  data.length = uint16_t(count - 1); data.universe = get16(p, 113);
  data.priority = p[108]; data.sequence = p[111]; data.options = p[112];
  return true;
}
size_t encodeSacnData(uint8_t* p, size_t capacity, const SacnSender& sender,
                      const uint8_t* slots, uint16_t length) {
  const size_t size = SACN_HEADER_SIZE + length;
  if (!p || length > SACN_CHANNEL_COUNT || capacity < size || (length && !slots)) return 0;
  std::memset(p, 0, size);
  put16(p, 0, 0x10); std::memcpy(p + 4, identifier, sizeof(identifier));
  put16(p, 16, uint16_t(0x7000 | (size - 16))); put32(p, 18, 4);
  std::memcpy(p + 22, sender.cid, 16);
  put16(p, 38, uint16_t(0x7000 | (size - 38))); put32(p, 40, 2);
  if (sender.name) {
    for (size_t i = 0; i < 63 && sender.name[i]; ++i) p[44 + i] = uint8_t(sender.name[i]);
  }
  p[108] = sender.priority; p[111] = sender.sequence; p[112] = sender.options;
  put16(p, 113, sender.universe);
  put16(p, 115, uint16_t(0x7000 | (size - 115))); p[117] = 2; p[118] = 0xa1;
  put16(p, 121, 1); put16(p, 123, uint16_t(length + 1));
  if (length) std::memcpy(p + 126, slots, length);
  return size;
}
}} // namespace nocte::net
