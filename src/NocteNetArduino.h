#pragma once

#include <Arduino.h>
#include <Udp.h>
#include "NocteNet.h"

namespace nocte { namespace net {

inline IPv4 fromArduino(const IPAddress& ip) {
  return IPv4(ip[0], ip[1], ip[2], ip[3]);
}
inline IPAddress toArduino(IPv4 ip) {
  return IPAddress(ip[0], ip[1], ip[2], ip[3]);
}

/** Wraps any Arduino UDP object, including WiFiUDP and EthernetUDP. */
class ArduinoUdpTransport final : public UdpTransport {
public:
  explicit ArduinoUdpTransport(UDP& udp) : udp_(udp) {}
  void stop() override { udp_.stop(); }
  bool begin(uint16_t port) override { return udp_.begin(port) != 0; }
  int parsePacket() override { return udp_.parsePacket(); }
  int read(uint8_t* data, size_t length) override { return udp_.read(data, length); }
  IPv4 remoteIP() const override { return fromArduino(udp_.remoteIP()); }
  bool beginPacket(IPv4 ip, uint16_t port) override {
    return udp_.beginPacket(toArduino(ip), port) != 0;
  }
  size_t write(const uint8_t* data, size_t length) override {
    return udp_.write(data, length);
  }
  bool endPacket() override { return udp_.endPacket() != 0; }
private:
  UDP& udp_;
};

class ArduinoRuntime final : public Runtime {
public:
  uint32_t nowMillis() const override { return millis(); }
  uint32_t randomBelow(uint32_t upperExclusive) override {
    return static_cast<uint32_t>(random(static_cast<long>(upperExclusive)));
  }
};

}} // namespace nocte::net
