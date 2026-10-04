# NocteNet

**Lighting protocols. Independent of your network interface.**

[![CI](https://github.com/IllumiNocte/NocteNet/actions/workflows/ci.yml/badge.svg)](https://github.com/IllumiNocte/NocteNet/actions/workflows/ci.yml)
![C++11](https://img.shields.io/badge/core-C%2B%2B11-blue)
![License](https://img.shields.io/badge/license-GPL--2.0--only-blue)

NocteNet is IllumiNocte's portable Art-Net / ArtRdm library, extracted from the
maintained uNodeArtNet implementation. The protocol core has no Arduino or ESP
header dependencies. An application supplies its UDP transport, clock, and random
source. Arduino adapters work with WiFiUDP or EthernetUDP without changing the
protocol implementation.

> Initial development version. sACN is not included yet. USB Ethernet is a planned
> application/platform integration, not an implemented USB driver in this library.
> Host tests and compile CI do not constitute Art-Net certification or live network HIL.

## Included

- ArtDmx / ArtNzs receive, ArtDmx transmit, and ArtSync callbacks.
- ArtPoll discovery, deferred unicast ArtPollReply, targeted polls, and legacy reply profile.
- ArtAddress / ArtIpProg callbacks and management replies.
- ArtTodRequest / ArtTodControl and split ArtTodData (up to 200 UIDs per datagram).
- ArtRdm receive/transmit with the DMX start code omitted on the network.
- Advertised RDM/discovery status and diagnostic counters for rejected packets.
- Fixed packet buffers and bounded cooperative receive/discard work.

Physical DMX/RDM is provided by the application (for example NocteDMX). RDM discovery,
queues, controller ownership, merging, failsafe, GUI, and licenses are not secretly
implemented by NocteNet.

## Arduino quick start

```cpp
#include <WiFiUdp.h>
#include <NocteNetArduino.h>

WiFiUDP udp;
nocte::net::ArduinoUdpTransport transport(udp);
nocte::net::ArduinoRuntime runtime;
ArtNetNode artnet(transport, runtime);

// After the application has connected Wi-Fi:
ArtNetNetworkConfig network = {};
network.ip = nocte::net::fromArduino(WiFi.localIP());
network.subnet = nocte::net::fromArduino(WiFi.subnetMask());
network.gateway = nocte::net::fromArduino(WiFi.gatewayIP());
WiFi.macAddress(network.mac);
network.dhcp = true;
// Check this result: 0 means bound successfully.
const uint8_t result = artnet.begin(network);
// Call artnet.read() frequently from loop().
```

Supply the identity of the same interface used by the socket. Network setup and
reconnection remain application responsibilities. See the complete
[Wi-Fi receiver](examples/WiFiDmxReceiver), [Ethernet receiver](examples/EthernetDmxReceiver),
[unicast sender](examples/UnicastDmxSender), and [discovery tool](examples/ArtPollDiscovery).
Wire-level Port-Address 0 may appear as U1 in controller software.

## Platform-neutral use and tests

Include `NocteNet.h` and implement `nocte::net::UdpTransport` and `Runtime`.
No Arduino mocks or real network are needed to test protocol behavior.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

Linux sanitizer build: add `-DNOCTENET_SANITIZERS=ON` when configuring.
CI tests Linux/Windows cores, ASan/UBSan, and all Arduino examples on ESP8266/S3.
See [architecture and contracts](docs/architecture.md) and [contribution guidelines](CONTRIBUTING.md).

## Roadmap

1. Qualify the extraction in uNode on ESP8266 and ESP32-S3 with live network/RDM tests.
2. Extract sACN into an optional module with explicit per-interface multicast handling.
3. Add native socket adapters and broader property/fuzz testing as needed.
4. Integrate S3 USB networking in uNode; preserve the protocol/driver boundary.

Future protocols can be separate optional modules. This is not a promise that
SigNet, KiNet, ShowNet, MQTT, or arbitrary transports are currently implemented.

## License and provenance

**GPL-2.0-only.** Derived from ArtnetnodeWifi by Charles Yarnold and Stephan Ruloff,
with subsequent uNodeArtNet / IllumiNocte maintenance. Original copyright headers
and the complete [license](LICENSE) are retained. Extraction and renaming do not
relicense third-party code. The original project is
[rstephan/ArtnetnodeWifi](https://github.com/rstephan/ArtnetnodeWifi).

The library's separation into a repository is not a licensing exemption for a
proprietary linked firmware. Product distribution/licensing must be reviewed
separately before distributing closed-source uNode/OEM firmware.
