#pragma once

#include <stddef.h>
#include <stdint.h>

namespace nocte { namespace net {

/** IPv4 octets in wire order; no integer-endianness or framework dependency. */
struct IPv4 {
  uint8_t octets[4];
  constexpr IPv4() : octets{0, 0, 0, 0} {}
  constexpr IPv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
    : octets{a, b, c, d} {}
  uint8_t operator[](size_t index) const { return octets[index]; }
  bool operator==(const IPv4& other) const {
    return octets[0] == other[0] && octets[1] == other[1]
      && octets[2] == other[2] && octets[3] == other[3];
  }
  bool operator!=(const IPv4& other) const { return !(*this == other); }
};

/**
 * One externally owned UDP socket, associated with one network interface.
 * Calls must be nonblocking. parsePacket() starts at most one datagram;
 * read() must never concatenate datagrams. remoteIP() identifies that packet.
 * beginPacket/write/endPacket assemble exactly one outgoing datagram.
 * An adapter must report short reads/writes and failed binds/sends honestly.
 * No Wi-Fi setup, DHCP, routing, multicast, or driver ownership is implied.
 */
class UdpTransport {
public:
  virtual ~UdpTransport() = default;
  virtual void stop() = 0;
  virtual bool begin(uint16_t port) = 0;
  virtual int parsePacket() = 0;
  virtual int read(uint8_t* data, size_t length) = 0;
  virtual IPv4 remoteIP() const = 0;
  virtual bool beginPacket(IPv4 destination, uint16_t port) = 0;
  virtual size_t write(const uint8_t* data, size_t length) = 0;
  virtual bool endPacket() = 0;
};

/** Injected monotonic uint32 millisecond clock and bounded random source. */
class Runtime {
public:
  virtual ~Runtime() = default;
  virtual uint32_t nowMillis() const = 0;
  /** Returns a value in [0, upperExclusive); upperExclusive is nonzero. */
  virtual uint32_t randomBelow(uint32_t upperExclusive) = 0;
};

template<typename T> inline T lesser(T a, T b) { return a < b ? a : b; }

}} // namespace nocte::net
