#include "NocteNetSacn.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); std::exit(1); } } while (0)
using namespace nocte::net;

// Independent wire fixture: don't round-trip only through the implementation.
static std::vector<uint8_t> packet(uint16_t length = 4, uint8_t cid = 1,
                                   uint8_t sequence = 1, uint8_t priority = 100,
                                   uint16_t universe = 1, uint8_t options = 0) {
  std::vector<uint8_t> p(126 + length, 0);
  p[1] = 0x10;
  const uint8_t pid[] = {'A','S','C','-','E','1','.','1','7',0,0,0};
  std::copy(pid, pid + 12, p.begin() + 4);
  const auto put16 = [&p](size_t i, uint16_t n) { p[i] = uint8_t(n >> 8); p[i + 1] = uint8_t(n); };
  put16(16, uint16_t(0x7000 | (p.size() - 16))); p[21] = 4;
  p[22] = cid; p[37] = 0xa5;
  put16(38, uint16_t(0x7000 | (p.size() - 38))); p[43] = 2;
  std::memcpy(p.data() + 44, "fixture", 7);
  p[108] = priority; p[111] = sequence; p[112] = options;
  put16(113, universe); put16(115, uint16_t(0x7000 | (p.size() - 115)));
  p[117] = 2; p[118] = 0xa1; p[122] = 1; put16(123, uint16_t(length + 1));
  for (uint16_t i = 0; i < length; ++i) p[126 + i] = uint8_t(i * 17 + cid);
  return p;
}
static SacnSourceResult feed(SacnSourceSelector& sources, const std::vector<uint8_t>& p, uint32_t now) {
  SacnData data{}; CHECK(decodeSacnData(p.data(), p.size(), data));
  return sources.accept(data, now);
}
struct Membership { IPv4 interfaceAddress, group; bool join; };
struct FakeTransport : SacnTransport {
  std::deque<std::vector<uint8_t>> incoming;
  std::vector<uint8_t> current, outgoing;
  std::vector<std::vector<uint8_t>> sent;
  std::vector<Membership> memberships;
  size_t position = 0, largestRead = 0;
  uint16_t boundPort = 0, destinationPort = 0;
  IPv4 destination, egress;
  bool joinOk = true, leaveOk = true, sendOk = true, beginSendOk = true;
  bool fastDiscard = false, shortWrite = false, shortRead = false;
  unsigned bindFailures = 0, stops = 0;
  void stop() override { ++stops; boundPort = 0; current.clear(); position = 0; }
  bool begin(uint16_t port) override {
    if (bindFailures) { --bindFailures; return false; }
    boundPort = port; return true;
  }
  int parsePacket() override {
    if (position < current.size() || incoming.empty()) return 0;
    current = incoming.front(); incoming.pop_front(); position = 0;
    return int(current.size());
  }
  int read(uint8_t* data, size_t length) override {
    size_t count = std::min(length, current.size() - position);
    if (shortRead && count) --count;
    largestRead = std::max(largestRead, count);
    std::copy(current.begin() + position, current.begin() + position + count, data);
    position += count; return int(count);
  }
  IPv4 remoteIP() const override { return IPv4(10,0,0,2); }
  bool beginPacket(IPv4 ip, uint16_t port) override {
    destination = ip; destinationPort = port; outgoing.clear(); return beginSendOk;
  }
  size_t write(const uint8_t* p, size_t size) override {
    if (shortWrite && size) --size;
    outgoing.insert(outgoing.end(), p, p + size); return size;
  }
  bool endPacket() override { if (sendOk) sent.push_back(outgoing); return sendOk; }
  bool joinMulticast(IPv4 iface, IPv4 group) override {
    memberships.push_back({iface, group, true}); return joinOk;
  }
  bool leaveMulticast(IPv4 iface, IPv4 group) override {
    memberships.push_back({iface, group, false}); return leaveOk;
  }
  bool beginMulticastPacket(IPv4 iface, IPv4 group, uint16_t port) override {
    egress = iface; return beginPacket(group, port);
  }
  bool discardPacket() override {
    if (!fastDiscard) return false;
    position = current.size(); return true;
  }
};
struct Capture {
  unsigned count = 0;
  std::vector<uint8_t> slots;
  uint8_t cid = 0;
  static void data(void* context, const SacnData& data) {
    auto& c = *static_cast<Capture*>(context);
    ++c.count; c.cid = data.cid[0]; c.slots.assign(data.slots, data.slots + data.length);
  }
};

static void wire() {
  for (uint16_t length : {uint16_t(0),uint16_t(1),uint16_t(4),uint16_t(512)}) {
    auto expected = packet(length);
    SacnData data{}; CHECK(decodeSacnData(expected.data(), expected.size(), data));
    CHECK(data.length == length && data.universe == 1 && data.priority == 100 && data.sequence == 1);
    CHECK(data.cid[0] == 1 && data.cid[15] == 0xa5);
    SacnSender sender{}; std::memcpy(sender.cid, data.cid, 16);
    sender.name = "fixture"; sender.universe = 1; sender.priority = 100; sender.sequence = 1;
    uint8_t encoded[638]; CHECK(encodeSacnData(encoded, sizeof(encoded), sender, data.slots, length) == expected.size());
    CHECK(std::equal(expected.begin(), expected.end(), encoded));
    CHECK(!encodeSacnData(encoded, expected.size() - 1, sender, data.slots, length));
  }
  SacnSender sender{}; sender.name = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-extra";
  sender.options = 0x40; sender.universe = 63999; sender.sequence = 255;
  uint8_t p[638]; CHECK(encodeSacnData(p, sizeof(p), sender, nullptr, 0) == 126);
  CHECK(p[107] == 0 && p[106] == '-');
  SacnData data{}; CHECK(decodeSacnData(p, 126, data)); CHECK(data.terminated() && data.universe == 63999);
  CHECK(!encodeSacnData(nullptr, 638, sender, nullptr, 0));
  CHECK(!encodeSacnData(p, 638, sender, nullptr, 1));
  CHECK(!encodeSacnData(p, 638, sender, p, 513));
  CHECK(sacnMulticastAddress(1) == IPv4(239,255,0,1));
  CHECK(sacnMulticastAddress(63999) == IPv4(239,255,249,255));
}
static void parser() {
  auto valid = packet(); SacnData data{};
  CHECK(!decodeSacnData(nullptr, 126, data));
  for (size_t n = 0; n < 126; ++n) CHECK(!decodeSacnData(valid.data(), n, data));
  for (size_t offset : {size_t(1),size_t(2),size_t(4),size_t(21),size_t(43),size_t(117),size_t(118),size_t(120),size_t(122),size_t(125)}) {
    auto bad = valid; bad[offset] ^= 1; CHECK(!decodeSacnData(bad.data(), bad.size(), data));
  }
  for (size_t offset : {size_t(16),size_t(38),size_t(115),size_t(123)}) {
    auto bad = valid; bad[offset] = bad[offset + 1] = 0; CHECK(!decodeSacnData(bad.data(), bad.size(), data));
  }
  auto bad = valid; bad[124] = 6; CHECK(!decodeSacnData(bad.data(), bad.size(), data));
  bad = valid; bad.resize(639); CHECK(!decodeSacnData(bad.data(), bad.size(), data));
  CHECK(decodeSacnData(valid.data(), valid.size(), data));
}
static void sources() {
  SacnSourceSelector s;
  CHECK(feed(s, packet(4,1,254,90), 100) == SacnSourceResult::Accepted);
  CHECK(s.activeCount() == 1 && s.selected()->frame[0] == 1 && s.selected()->frame[4] == 0);
  CHECK(feed(s, packet(4,1,254,90), 101) == SacnSourceResult::SequenceDrop);
  CHECK(feed(s, packet(4,1,253,90), 102) == SacnSourceResult::SequenceDrop);
  CHECK(feed(s, packet(4,1,255,90), 103) == SacnSourceResult::Accepted);
  CHECK(feed(s, packet(4,1,0,90), 104) == SacnSourceResult::Accepted);
  CHECK(feed(s, packet(4,1,128,90), 105) == SacnSourceResult::SequenceDrop);
  CHECK(feed(s, packet(4,2,1,120), 106) == SacnSourceResult::Accepted);
  CHECK(feed(s, packet(4,1,1,90), 107) == SacnSourceResult::PriorityDrop);
  CHECK(s.activeCount() == 2 && s.highestPriority() == 120 && s.selected()->cid[0] == 2);
  CHECK(feed(s, packet(4,2,2,120,1,0x40), 108) == SacnSourceResult::Terminated);
  CHECK(s.activeCount() == 1 && s.selected()->priority == 90);
  CHECK(s.diagnostics().sequenceDrops == 3 && s.diagnostics().priorityDrops == 1 && s.diagnostics().streamTerminated == 1);
  s.clear(); CHECK(!s.selected() && s.diagnostics().sequenceDrops == 3);
  SacnData invalid{}; CHECK(s.accept(invalid, 109) == SacnSourceResult::Invalid);
  // Equal-priority selection is latest complete source, not per-slot HTP.
  CHECK(feed(s, packet(1,10,1,100), 200) == SacnSourceResult::Accepted);
  CHECK(feed(s, packet(1,5,1,100), 201) == SacnSourceResult::Accepted);
  CHECK(s.selected()->frame[0] == 5);
  CHECK(feed(s, packet(1,3,1,100), 202) == SacnSourceResult::Accepted);
  CHECK(s.activeCount() == 2 && s.selected()->cid[0] == 3);
}
static void timeout() {
  SacnSourceSelector s;
  feed(s, packet(4,1,1,90), 100); feed(s, packet(4,2,1,120), 101);
  CHECK(!s.expire(2599)); CHECK(s.expire(2600)); CHECK(s.selected()->priority == 120);
  CHECK(s.expire(2601) && !s.selected() && s.diagnostics().sourceTimeouts == 2);
  CHECK(!s.expire(5000));
  feed(s, packet(4,1,1), 0xffffff00u);
  CHECK(!s.expire(0xffffff00u + 2499u)); CHECK(s.expire(0xffffff00u + 2500u));
  s.clear(); feed(s, packet(4,1,1,90), 100); feed(s, packet(4,2,1,120), 101);
  CHECK(feed(s, packet(4,1,2,90), 2000) == SacnSourceResult::PriorityDrop);
  CHECK(s.expire(2601) && s.selected()->priority == 90 && s.selected()->sequence == 2);
}
static void network() {
  FakeTransport t; SacnNode n(t); IPv4 a(10,0,0,1), b(192,168,7,1);
  CHECK(n.begin(a, 1) && n.multicastJoined() && t.boundPort == 5568);
  CHECK(n.begin(b, 256));
  CHECK(t.memberships.size() == 3 && !t.memberships[1].join && t.memberships[1].interfaceAddress == a);
  CHECK(t.memberships[2].join && t.memberships[2].interfaceAddress == b && t.memberships[2].group == IPv4(239,255,1,0));
  n.stop(); CHECK(!n.ready() && !n.multicastJoined());
  CHECK(n.diagnostics().multicastJoins == 2 && n.diagnostics().multicastLeaves == 2);
  t.joinOk = false; CHECK(n.begin(a, 1) && !n.multicastJoined());
  CHECK(n.diagnostics().multicastJoinFailures == 1);
  t.joinOk = true; t.bindFailures = 1; CHECK(n.begin(a, 1));
  CHECK(!n.multicastJoined() && n.diagnostics().multicastLeaves == 3);
  t.bindFailures = 2; CHECK(!n.begin(a, 1) && !n.ready() && !n.multicastJoined());
  CHECK(n.begin(a, 1)); t.leaveOk = false; n.stop();
  CHECK(n.diagnostics().multicastLeaveFailures == 1);
  CHECK(n.begin(IPv4(), 1) && !n.multicastJoined());
}
static void receive() {
  FakeTransport t; SacnNode n(t); Capture c; n.setDataCallback(Capture::data, &c);
  CHECK(!n.read()); CHECK(n.begin(IPv4(10,0,0,1), 1));
  t.incoming.push_back(packet()); t.incoming.push_back(packet(4,2));
  CHECK(n.read() && c.count == 1 && t.incoming.size() == 1);
  CHECK(n.read() && c.count == 2 && c.cid == 2); CHECK(!n.read());
  t.incoming.push_back(packet(4,1,1,100,2)); CHECK(n.read() && c.count == 2);
  CHECK(n.diagnostics().wrongUniversePackets == 1 && n.diagnostics().lastWrongUniverse == 2);
  t.incoming.push_back({1,2,3}); CHECK(n.read() && n.diagnostics().malformedPackets == 1);
  t.shortRead = true; t.incoming.push_back(packet()); CHECK(n.read());
  CHECK(c.count == 2 && n.diagnostics().malformedPackets == 2);
  t.shortRead = false; CHECK(n.read()); // drain the one unread byte
  t.incoming.push_back(packet()); CHECK(n.read() && c.count == 3);
  t.incoming.push_back(std::vector<uint8_t>(2000, 0)); t.incoming.push_back(packet());
  CHECK(n.read()); CHECK(n.diagnostics().malformedPackets == 3);
  t.largestRead = 0;
  for (unsigned i = 0; i < 8; ++i) CHECK(n.read() && c.count == 3);
  CHECK(t.largestRead <= 256); CHECK(n.read() && c.count == 4);
  t.fastDiscard = true; t.incoming.push_back(std::vector<uint8_t>(65500,0)); t.incoming.push_back(packet());
  CHECK(n.read() && n.read() && c.count == 5);
  // Separate instances have separate receive buffers, callback contexts and state.
  FakeTransport other; SacnNode m(other); Capture d; m.setDataCallback(Capture::data, &d);
  CHECK(m.begin(IPv4(192,168,7,1), 2)); other.incoming.push_back(packet(1,9,1,100,2));
  CHECK(m.read() && d.count == 1 && c.count == 5 && n.diagnostics().udpPackets == 8);
}
static void transmit() {
  FakeTransport t; SacnNode n(t); SacnSender s{};
  s.cid[0] = 1; s.cid[15] = 0xa5; s.name = "fixture";
  s.universe = 1; s.priority = 100; s.sequence = 1;
  const uint8_t slots[] = {1,18,35,52};
  CHECK(!n.send(s, slots, 4)); CHECK(n.begin(IPv4(192,168,7,1), 1));
  CHECK(n.send(s, slots, 4) && t.sent.back() == packet());
  CHECK(t.egress == IPv4(192,168,7,1) && t.destination == IPv4(239,255,0,1) && t.destinationPort == 5568);
  t.shortWrite = true; CHECK(!n.send(s, slots, 4));
  t.shortWrite = false; t.sendOk = false; CHECK(!n.send(s, slots, 4));
  t.sendOk = true; t.beginSendOk = false; CHECK(!n.send(s, slots, 4));
  t.beginSendOk = true; CHECK(n.send(s, slots, 4));
}
static void legacy() {
  // Characterization, NOT declarations of normative E1.31 behavior.
  auto p = packet(); SacnData d{};
  p[16] = 0; p[17] = 110; p[108] = 255; p[112] = 0xa0;
  CHECK(decodeSacnData(p.data(), p.size(), d));
  CHECK(d.priority == 255 && d.options == 0xa0); // preview/sync currently not interpreted
  SacnSourceSelector s; feed(s, packet(1,1,1,100), 100); feed(s, packet(1,2,1,100), 200);
  feed(s, packet(1,1,2,100,1,0x40), 201);
  CHECK(s.activeCount() == 1);
  feed(s, packet(1,2,2,100), 202);
  CHECK(s.activeCount() == 2); // inherited first-free-slot-before-later-CID behavior
}
static void sweep() {
  uint32_t seed = 0x10203040;
  for (unsigned i = 0; i < 15000; ++i) {
    auto p = packet(uint16_t(i % 513));
    seed = seed * 1664525u + 1013904223u;
    if (i & 1) p.resize(seed % 700);
    else p[seed % p.size()] ^= uint8_t(1u << ((seed >> 16) & 7));
    SacnData d{};
    if (decodeSacnData(p.data(), p.size(), d)) {
      CHECK(d.length <= 512 && d.slots + d.length <= p.data() + p.size());
      SacnSourceSelector sources; sources.accept(d, i);
    }
  }
}
int main(int argc, char** argv) {
  CHECK(argc == 2);
  if (!std::strcmp(argv[1], "wire")) wire();
  else if (!std::strcmp(argv[1], "parser")) parser();
  else if (!std::strcmp(argv[1], "sources")) sources();
  else if (!std::strcmp(argv[1], "timeout")) timeout();
  else if (!std::strcmp(argv[1], "network")) network();
  else if (!std::strcmp(argv[1], "receive")) receive();
  else if (!std::strcmp(argv[1], "transmit")) transmit();
  else if (!std::strcmp(argv[1], "legacy")) legacy();
  else if (!std::strcmp(argv[1], "sweep")) sweep();
  else CHECK(false);
}
