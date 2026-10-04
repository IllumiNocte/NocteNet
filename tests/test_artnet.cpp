#include "NocteNet.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
  std::exit(1); } } while (0)

using nocte::net::IPv4;
struct FakeRuntime : nocte::net::Runtime {
  uint32_t now = 100;
  uint32_t jitter = 500;
  uint32_t nowMillis() const override { return now; }
  uint32_t randomBelow(uint32_t upper) override { return jitter % upper; }
};
struct Datagram { IPv4 ip; uint16_t port; std::vector<uint8_t> bytes; };
struct FakeUdp : nocte::net::UdpTransport {
  std::deque<Datagram> incoming;
  std::vector<Datagram> sent;
  Datagram current{}, outgoing{};
  size_t position = 0;
  bool bindOk = true, sendOk = true, shortRead = false, shortWrite = false;
  uint16_t boundPort = 0;
  int maxRead = 0;
  void stop() override { boundPort = 0; current.bytes.clear(); position = 0; }
  bool begin(uint16_t port) override { if (bindOk) boundPort = port; return bindOk; }
  int parsePacket() override {
    if (position < current.bytes.size()) return 0;
    if (incoming.empty()) return 0;
    current = incoming.front(); incoming.pop_front(); position = 0;
    return static_cast<int>(current.bytes.size());
  }
  int read(uint8_t* data, size_t length) override {
    const size_t count = std::min(length, current.bytes.size() - position);
    const size_t actual = shortRead && count ? count - 1 : count;
    std::copy(current.bytes.begin() + position, current.bytes.begin() + position + actual, data);
    position += actual; maxRead = std::max(maxRead, static_cast<int>(actual));
    return static_cast<int>(actual);
  }
  IPv4 remoteIP() const override { return current.ip; }
  bool beginPacket(IPv4 ip, uint16_t port) override { outgoing = {ip, port, {}}; return sendOk; }
  size_t write(const uint8_t* data, size_t length) override {
    if (shortWrite && length) --length;
    outgoing.bytes.insert(outgoing.bytes.end(), data, data + length); return length;
  }
  bool endPacket() override { if (sendOk) sent.push_back(outgoing); return sendOk; }
  void push(std::vector<uint8_t> bytes, IPv4 ip = IPv4(2,0,0,100)) {
    incoming.push_back({ip, ARTNET_PORT, bytes});
  }
};
struct Fixture {
  FakeUdp udp; FakeRuntime clock; ArtNetNode node{udp, clock};
  ArtNetNetworkConfig network{};
  Fixture() {
    network.ip = IPv4(2,0,0,1); network.subnet = IPv4(255,0,0,0);
    network.gateway = IPv4(2,0,0,254); network.mac[5] = 1;
    CHECK(node.begin(network) == 0); CHECK(udp.boundPort == ARTNET_PORT);
    node.setDirection(true);
  }
  uint16_t feed(std::vector<uint8_t> bytes) { udp.push(bytes); return node.read(); }
};
static std::vector<uint8_t> packet(uint16_t op, size_t size) {
  std::vector<uint8_t> p(size);
  if (size >= 8) std::copy(ArtNetNode::artnetId, ArtNetNode::artnetId + 8, p.begin());
  if (size >= 10) { p[8] = op & 255; p[9] = op >> 8; }
  if (size >= 12) p[11] = ARTNET_PROTOCOL_VERSION;
  return p;
}
static int callbacks = 0;
static uint16_t lastUniverse = 0, lastLength = 0;
static uint8_t lastSequence = 0, firstSlot = 0;
static void dmx(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* bytes) {
  ++callbacks; lastUniverse = universe; lastLength = length;
  lastSequence = sequence; firstSlot = bytes[0];
}
static std::vector<uint8_t> dmxPacket(uint16_t count = 512) {
  auto p = packet(OpDmx, ARTNET_DMX_START_LOC + count);
  p[12] = 123; p[13] = 7; p[14] = 0x45; p[15] = 0x23;
  p[16] = count >> 8; p[17] = count & 255; p[18] = 42;
  return p;
}
static void testDmx() {
  Fixture f; callbacks = 0; f.node.setArtDmxCallback(dmx);
  CHECK(f.feed(dmxPacket()) == OpDmx); CHECK(callbacks == 1);
  CHECK(lastUniverse == 0x2345 && lastLength == 512 && lastSequence == 123 && firstSlot == 42);
  CHECK(f.node.getIncomingPhysical() == 7);
  CHECK(f.node.getSenderIp() == IPv4(2,0,0,100));
  for (uint16_t count : {uint16_t(1), uint16_t(3)}) CHECK(f.feed(dmxPacket(count)) == 0);
  auto p = dmxPacket(2); p[17] = 4; CHECK(f.feed(p) == 0);
  CHECK(callbacks == 1 && f.node.getParserDiagnostics().malformedPackets == 3);
}
static void testParserDiagnostics() {
  Fixture f;
  CHECK(f.feed({1,2,3}) == 0); CHECK(f.node.getParserDiagnostics().shortPackets == 1);
  auto p = dmxPacket(2); p[0] = 0; CHECK(f.feed(p) == 0);
  CHECK(f.node.getParserDiagnostics().invalidIdPackets == 1);
  p = dmxPacket(2); p[11] = 13; CHECK(f.feed(p) == 0);
  CHECK(f.node.getParserDiagnostics().unsupportedProtocolPackets == 1);
  CHECK(f.feed(packet(0xffff, 14)) == 0xffff);
  CHECK(f.node.getParserDiagnostics().unsupportedOpcodes == 1);
  f.udp.shortRead = true; CHECK(f.feed(dmxPacket(2)) == 0);
  CHECK(f.node.getParserDiagnostics().malformedPackets == 1);
  f.udp.shortRead = false; f.node.read();
  CHECK(f.feed(dmxPacket(2)) == OpDmx); // incomplete packet must not stall the socket
}
static void testOversizedRecovery() {
  Fixture f; callbacks = 0; f.node.setArtDmxCallback(dmx);
  f.udp.push(std::vector<uint8_t>(65507, 0xaa)); f.udp.push(dmxPacket(2));
  CHECK(f.node.read() == 0); CHECK(f.udp.maxRead <= 256);
  for (int i = 0; i < 300 && callbacks == 0; ++i) f.node.read();
  CHECK(callbacks == 1 && f.node.getParserDiagnostics().oversizedPackets == 1);
}
static void testPoll() {
  Fixture f; auto p = packet(OpPoll, 14);
  CHECK(f.feed(p) == OpPoll); CHECK(f.udp.sent.empty());
  CHECK(f.feed(p) == OpPoll); // duplicate must not add another delayed reply
  f.clock.now = 599; f.node.read(); CHECK(f.udp.sent.empty());
  f.clock.now = 600; f.node.read(); CHECK(f.udp.sent.size() == 1);
  const auto& r = f.udp.sent[0];
  CHECK(r.ip == IPv4(2,0,0,100) && r.port == ARTNET_PORT && r.bytes.size() == 239);
  CHECK(r.bytes[10] == 2 && r.bytes[13] == 1 && r.bytes[211] == 1);
  CHECK(f.node.getLastPollMillis() == 600);
  Fixture wrap; wrap.clock.now = 0xffffff00U; wrap.feed(p);
  wrap.clock.now = 243; wrap.node.read(); CHECK(wrap.udp.sent.empty());
  wrap.clock.now = 244; wrap.node.read(); CHECK(wrap.udp.sent.size() == 1);
}
static void testTargetedPollAndRebind() {
  Fixture f; f.node.setStartingUniverse(100);
  auto p = packet(OpPoll, 18); p[12] = 0x20; p[15] = 99;
  CHECK(f.feed(p) == 0); f.clock.now += 1000; f.node.read(); CHECK(f.udp.sent.empty());
  p[15] = 101; p[17] = 100; CHECK(f.feed(p) == OpPoll);
  f.network.ip = IPv4(10,0,0,1); CHECK(f.node.begin(f.network) == 0);
  f.clock.now += 1000; f.node.read(); CHECK(f.udp.sent.empty());
  f.feed(p); f.clock.now += 1000; f.node.read(); CHECK(f.udp.sent[0].bytes[10] == 10);
  f.feed(p); f.udp.bindOk = false; CHECK(f.node.begin(f.network) == 1);
  const size_t sent = f.udp.sent.size();
  f.clock.now += 1000; f.node.read(); CHECK(f.udp.sent.size() == sent);
}
static void testTransmit() {
  Fixture f; f.node.setLength(3); f.node.setUniverse(0x2345);
  f.node.setByte(0, 42); f.node.setByte(1, 43); f.node.setByte(2, 44);
  IPv4 targets[] = {IPv4(2,0,0,10), IPv4(2,0,0,11)};
  CHECK(f.node.write(targets, 2) == 2);
  CHECK(f.udp.sent[0].bytes == f.udp.sent[1].bytes);
  const auto& bytes = f.udp.sent[0].bytes;
  CHECK(bytes.size() == 22 && bytes[17] == 4 && bytes[21] == 0);
  CHECK(bytes[14] == 0x45 && bytes[15] == 0x23 && bytes[18] == 42);
  f.udp.sendOk = false; CHECK(f.node.write(targets[0]) == 0);
  f.udp.sendOk = true; f.udp.shortWrite = true; CHECK(f.node.write(targets[0]) == 0);
  CHECK(f.node.write(nullptr, 0) == 0);
  f.node.setLength(1); CHECK(f.node.write(targets[0]) == 0);
}
static int rdmCalls = 0;
static void rdm(const ArtRdmInfo& info) {
  ++rdmCalls; CHECK(info.rdmLength == 25 && info.rdmData[0] == 1);
  CHECK(info.portAddress == 0x2345 && info.senderIP == IPv4(2,0,0,100));
}
static void testRdmAndTod() {
  Fixture f; rdmCalls = 0; f.node.setArtRdmCallback(rdm);
  auto p = packet(OpRdm, 49); p[12] = 1; p[21] = 0x23; p[23] = 0x45;
  p[24] = 1; p[25] = 24;
  CHECK(f.feed(p) == OpRdm && rdmCalls == 1);
  p[25] = 25; CHECK(f.feed(p) == 0 && rdmCalls == 1);
  uint8_t data[25] = {1,24}; CHECK(f.node.sendArtRdm(IPv4(2,0,0,10), 0x2345, data, 25) == 1);
  CHECK(f.udp.sent[0].bytes.size() == 49 && f.udp.sent[0].bytes[24] == 1);
  CHECK(f.node.sendArtRdm(IPv4(), 0x8000, data, 25) == 0);
  std::vector<uint8_t> uids(201 * 6, 0xaa);
  CHECK(f.node.sendArtTodData(IPv4(2,0,0,10), 0x2345, uids.data(), 201) == 2);
  CHECK(f.udp.sent[1].bytes.size() == 1228 && f.udp.sent[1].bytes[27] == 200);
  CHECK(f.udp.sent[2].bytes.size() == 34 && f.udp.sent[2].bytes[26] == 1);
  CHECK(f.udp.sent[2].bytes[24] == 0 && f.udp.sent[2].bytes[25] == 201);
  CHECK(f.node.sendArtTodData(IPv4(), 0, nullptr, 1) == 0);
  CHECK(f.node.sendArtTodData(IPv4(), 0, nullptr, 0, false) == 1);
  CHECK(f.udp.sent.back().bytes[22] == ARTNET_TOD_NAK);
}
static void testReplyProfiles() {
  PollReply r; r.setOutputEnabled(0); r.setRdmCapable(true); r.setRdmEnabled(0, true);
  CHECK((r.packet.Status1 & 2) && !(r.packet.GoodOutputB[0] & 0x80));
  r.setRdmDiscoveryRunning(0, true); CHECK(!(r.packet.GoodOutputB[0] & 0x20));
  r.setLegacyArtNet3Mode(true); const uint8_t* legacy = r.printPacket(); CHECK(legacy[213] == 0);
  CHECK(r.packet.GoodOutputB[0] != 0); // legacy must not mutate live configuration
  r.setLegacyArtNet3Mode(false); CHECK(r.printPacket()[213] == r.packet.GoodOutputB[0]);
  r.setShortName("123456789012345678901234"); CHECK(r.packet.PortName[17] == 0);
  r.setStartingUniverse(0xffff); CHECK(r.getStartingUniverse() == 0x7fff);
}
static void testIsolationAndMalformedSweep() {
  Fixture a, b; a.node.setStartingUniverse(99); CHECK(b.node.getStartingUniverse() == 0);
  const uint16_t ops[] = {OpDmx,OpNzs,OpPoll,OpSync,OpPollReply,OpAddress,OpIpProg,OpTodRequest,OpTodControl,OpRdm};
  uint32_t state = 123456;
  for (uint16_t op : ops) for (size_t length = 1; length <= 1500; ++length) {
    auto p = packet(op, length);
    for (size_t i = 12; i < length; ++i) {
      state = state * 1664525U + 1013904223U; p[i] = state >> 24;
    }
    a.feed(p);
    for (int i = 0; i < 8; ++i) a.node.read();
  }
}
int main(int argc, char** argv) {
  struct Test { const char* name; void (*run)(); };
  const Test tests[] = {
    {"dmx", testDmx}, {"diagnostics", testParserDiagnostics},
    {"oversize", testOversizedRecovery}, {"poll", testPoll},
    {"rebind", testTargetedPollAndRebind}, {"transmit", testTransmit},
    {"rdm-tod", testRdmAndTod}, {"reply-profiles", testReplyProfiles},
    {"malformed-sweep", testIsolationAndMalformedSweep}
  };
  bool found = false;
  for (const auto& test : tests) if (argc == 1 || std::strcmp(argv[1], test.name) == 0) {
    test.run(); std::printf("PASS: %s\n", test.name); found = true;
  }
  return found ? 0 : 2;
}
