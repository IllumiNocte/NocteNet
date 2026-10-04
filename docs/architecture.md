# Architecture and transport contract

NocteNet is a protocol library, not a networking framework or a physical DMX driver.
The initial module is the Art-Net / ArtRdm implementation extracted from uNodeArtNet.

## Boundaries

- `NocteNet.h`, `ArtNetNode`, `PollReply`, and `NocteNetTransport.h`: portable C++11.
- `NocteNetArduino.h`: Arduino UDP / IPAddress / millis / random adaptation only.
- Application: drivers, IP configuration, interface selection, link state, persistence,
  source merging, failsafe, controller ownership, and physical DMX/RDM scheduling.
- NocteDMX: physical DMX/RDM transport; NocteNet has no dependency on it.

UDP transport and Runtime are externally owned, must outlive ArtNetNode, and are
not deleted by the library. Do not copy an active node or share one socket between
nodes. Calls and callbacks run cooperatively on one application context; instances
are not internally synchronized. Call read() frequently, even with no DMX traffic,
to service delayed management responses. Callbacks must not re-enter read() or send
through the same node: its packet buffer is shared between RX and ArtDmx TX.
Consume/copy callback data before returning. After returning, it is borrowed and
may be overwritten by the next read or transmit call.

## UDP adapter

The adapter is a narrow UDP socket API, not a Wi-Fi/Ethernet abstraction. It must
keep packet boundaries, return the actual current packet length, and identify
the sender of that packet. Reads/writes may be partial and must report their actual
length. A truncated receive buffer must never be presented as a complete datagram.
Each read() invocation processes at most one ordinary datagram or drains one
256-byte chunk of a rejected oversized/incomplete datagram. It does not spin until
the whole receive queue is empty. This bounded discard behavior is retained from
uNodeArtNet, including its validated opt-in fast discard for suitable transports.

Set `setDiscardUnreadPacketOnNextParse(true)` ONLY if the transport guarantees that
the next parsePacket() releases unread bytes of the prior packet. ESP8266 WiFiUDP
has that behavior; do not assume ESP32 NetworkUDP or an arbitrary Ethernet library
does. The default is the portable bounded drain path.

begin(network) stops/reopens the socket and invalidates all pending ArtPoll replies,
including when binding fails. The caller handles link/IP changes and retry policy.
All sends return success/failure from the adapter; no driver retries are invented.
The current fragmented transmit API cannot abort a partially written datagram;
applications must not interpret a zero return as evidence that nothing was sent.

## Interface identity and future modules

Supply the IP, subnet, gateway, MAC, and DHCP flag of the SAME interface used by the
socket. Wi-Fi, wired Ethernet, and USB Ethernet can each host that socket. A future
USB adapter should use the platform IP stack, not send raw USB frames from ArtNetNode.
Configuring multicast membership is deliberately not claimed by the initial UDP
adapter: sACN extraction will require an explicit multicast join/leave capability
associated with a selected interface, rather than calling WiFi/lwIP from the core.

Concurrent Wi-Fi and Ethernet/USB need explicit application policy: bind separate
sockets/state, reply through the ingress interface, avoid unintended cross-interface
forwarding, and prevent duplicate frames or feedback loops. This initial release
does not implement a multi-interface router. It retains uNode's single advertised
physical port behavior; a universal multiport node is not claimed.

## Time and tests

Runtime supplies a monotonic uint32 millisecond clock (wrap permitted) and random
values below a nonzero bound. Delayed replies use signed wrap-safe deadline checks.
The test Runtime is deterministic, so no real sleeps or Arduino shims are required.
Host tests exercise wire format, parser rejection/recovery, ArtPoll scheduling,
rebinds, instance isolation, ArtRdm and TOD fragmentation. A deterministic malformed
packet sweep is a regression test, not a substitute for continuous fuzzing or HIL.

## Migration from uNodeArtNet

Include `NocteNetArduino.h`, construct ArduinoUdpTransport and ArduinoRuntime, then
construct ArtNetNode(transport, runtime). IP fields and targets now use `IPv4`;
use `fromArduino()` / `toArduino()` explicitly at the application boundary.
The protocol class/callback names are retained for this first extraction to keep
behavior reviewable. There is no implicit Wi-Fi startup or compatibility singleton.
