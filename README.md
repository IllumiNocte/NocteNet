# NocteNet

**Lighting protocols. Independent of your network interface.**

[![CI](https://github.com/IllumiNocte/NocteNet/actions/workflows/ci.yml/badge.svg)](https://github.com/IllumiNocte/NocteNet/actions/workflows/ci.yml)
![C++11](https://img.shields.io/badge/core-C%2B%2B11-blue)
![License](https://img.shields.io/badge/license-GPL--2.0--only-blue)

NocteNet is IllumiNocte's standalone, portable library for Art-Net, ArtRdm and
optional sACN data. It provides reusable protocol components for lighting devices,
diagnostic tools and custom applications, without depending on a particular
product firmware. The C++11 protocol core has no Arduino or ESP header dependencies.
An application supplies its UDP transport, clock and random source. Arduino
adapters work with WiFiUDP or EthernetUDP without changing the protocol implementation.

> Initial development version. sACN covers the existing zero-start-code data subset,
> not the whole E1.31 specification. Network configuration and interface drivers,
> including USB networking, are application/platform responsibilities.
> Host tests and compile CI do not constitute Art-Net certification or live network HIL.

## Included

- ArtDmx / ArtNzs receive, ArtDmx transmit, and ArtSync callbacks.
- ArtPoll discovery, deferred unicast ArtPollReply, targeted polls, and legacy reply profile.
- ArtAddress / ArtIpProg callbacks and management replies.
- ArtTodRequest / ArtTodControl and split ArtTodData (up to 200 UIDs per datagram).
- ArtRdm receive/transmit with the DMX start code omitted on the network.
- Advertised RDM/discovery status and diagnostic counters for rejected packets.
- Fixed packet buffers and bounded cooperative receive/discard work.
- Optional sACN packet encoding/decoding, one-universe receive/transmit, explicit
  multicast membership/egress adaptation, and a separate two-source selection policy.

Physical DMX/RDM can be provided by a separate library such as
[NocteDMX](https://github.com/IllumiNocte/NocteDMX). RDM discovery, queues,
controller ownership, HTP/LTP merging and failsafe remain application responsibilities;
NocteNet does not prescribe a product's signal-flow or control policy.

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

## Optional sACN data module

Include `NocteNetSacn.h`. The standalone `encodeSacnData()` / `decodeSacnData()`
codec needs no socket or clock. `SacnNode` accepts an application-owned
`SacnTransport` with explicit interface-aware multicast join, leave and send
operations; no generic Arduino UDP adapter can silently promise those capabilities.
Each `read()` handles one datagram or one 256-byte discard chunk. A callback receives
borrowed metadata/slots; the application decides whether to use or reject them.

`SacnSourceSelector` is an optional, separately usable two-source selection policy:
sequence tracking, highest priority, latest timestamp on equal
priority, 2.5-second source expiry. It is **not an HTP merge**. The codec and selector
retain known initial limitations, documented in [sACN contracts and gaps](docs/sacn.md).
Do not infer E1.31 certification from parser tests.

See the [protocol-only example](examples/SacnDataCodec). CMake can omit all sACN
implementation objects with `-DNOCTENET_SACN=OFF`; Arduino/PlatformIO uses ordinary
dead-code elimination when no sACN API is referenced. Art-Net headers/API are unchanged.

## Roadmap

1. Expand hardware-in-the-loop and controller interoperability coverage for
   Art-Net/ArtRdm and sACN on supported platforms.
2. Qualify sACN with live multicast, input/output, rebind and malformed traffic.
   Then address its documented E1.31 gaps as separately tested behavior changes.
3. Add native socket adapters and broader property/fuzz testing as needed.
4. Document and qualify further UDP/multicast adapters for Wi-Fi, Ethernet and
   USB-network interfaces while preserving the protocol/driver boundary.

Future protocols can be separate optional modules. This is not a promise that
SigNet, KiNet, ShowNet, MQTT, or arbitrary transports are currently implemented.

## Background

The µNode project motivated NocteNet's creation: its lighting protocols needed a
reusable core independent of hardware and network interfaces. NocteNet is maintained
as a separate library, with its own API, examples, tests and roadmap; using it does
not require µNode firmware.

## License and provenance

**GPL-2.0-only.** Derived from ArtnetnodeWifi by Charles Yarnold and Stephan Ruloff,
with subsequent uNodeArtNet / IllumiNocte maintenance. Original copyright headers
and the complete [license](LICENSE) are retained. Extraction and renaming do not
relicense third-party code. The original project is
[rstephan/ArtnetnodeWifi](https://github.com/rstephan/ArtnetnodeWifi).

The library's separation into a repository is not a licensing exemption for a
proprietary linked firmware. Product distribution/licensing must be reviewed
separately before distributing closed-source firmware or OEM products.
