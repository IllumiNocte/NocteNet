# sACN data module: contracts and qualification gaps

The 0.2.0 module extracts uNode's existing zero-start-code sACN data implementation.
This is a behavior-preserving migration, **not a complete E1.31 implementation**.
No Arduino, Wi-Fi, ESP, lwIP, DMX driver, GUI, credentials or licensing policy is
required by these C++11 sources. No dynamic allocation is used by the core.

## Components

- `encodeSacnData` / `decodeSacnData`: bounded packet codec; metadata and slot views
  borrow the input buffer. On decode failure the caller must not use the output.
  Sender CID, universe, priority, sequence, options and name are explicit arguments.
  The sender name must be a valid C string or null. Encoder input buffers must not
  alias its destination. The codec permits zero slots; an application may impose a
  minimum one-slot frame, as uNode does. No implicit CID or sequence generator exists.
- `SacnNode`: one universe per socket/interface; fixed 638-byte buffer. `begin`
  releases old membership before rebinding, joins the new interface/group and binds
  UDP 5568. Failed join permits the existing unicast fallback. Failed bind after a
  successful join releases membership before retrying unicast. Diagnostics survive
  stop/rebind. The application owns retry timing. Call `stop` before destroying the
  transport or changing its underlying interface resources.
- `SacnTransport`: explicit join/leave and multicast TX for the requested interface.
  Driver adapters own any stale-address cleanup; the core never invents an all-NIC
  leave. An adapter may provide a constant-time `discardPacket`; otherwise rejected
  oversized/partial packets drain in at most 256-byte chunks per `read`. A partial
  read is counted malformed and never delivered as a complete packet. Datagrams
  exceeding 638 bytes are rejected once, then discarded without unbounded work.
- `SacnSourceSelector`: optional application selection policy. Pass the clock value
  explicitly. Two fixed source/frame slots track CIDs, sequences and priorities.
  The lower-priority source remains warm, termination removes a source, and expiry
  uses unsigned clock subtraction. `selected()` borrows source storage until the
  next accept/expire/clear. The caller decides when to apply a selected frame or
  enter failsafe. `clear()` retains cumulative diagnostics.

Callbacks must consume/copy borrowed packet data before returning. They must not
re-enter `read`, `send`, `begin` or `stop`: RX and TX share the node's buffer.
Instances and transports must not be copied/shared or concurrently called without
external synchronization. Different instances have independent buffers/counters.

## Deliberately retained behavior and known gaps

The parser checks fixed identifiers/vectors/addressing, minimum PDU lengths,
property bounds and start code zero. It retains the previous acceptance of
non-exact PDU lengths/flags, trailing bytes, out-of-spec priorities/universes and
uninterpreted option bits. It does **not** implement synchronization packets,
universe discovery, per-address priority (PAP), preview suppression or full
normative validation. Termination is recognized by option 0x40. A future strict
parser/profile needs its own compatibility review and tests.

The selector retains uint8 sequence-distance <128, source expiry at >=2500 ms,
oldest-slot replacement when both slots are occupied, and timestamp-based
whole-frame selection at equal priority. Equal-priority HTP merge is absent.
The initial first-free-slot lookup may pick an inactive earlier slot before a
later matching CID, creating duplicate tracking after termination/expiry. Equal
priority timestamp comparison is not wrap-aware. Both are explicitly characterized
as legacy gaps, not intended specification behavior; correcting them is separate
from this extraction. Applications needing another merge/sequence policy may use
`SacnNode` without this selector.

## Application/platform responsibilities

Radio/link setup, DHCP, IP identity, reconnection, credentials, frame gating,
physical DMX, source merging, failsafe and user-facing diagnostics stay outside the
protocol module. Multicast receive membership and transmit routing must both be
qualified on every adapter. The initial uNode ESP32 NetworkUDP adapter retains
single-radio default-route transmission; that is **not** qualified multi-NIC
egress isolation. Future USB/Wi-Fi transports must enforce the interface argument
and must not treat the existing adapter as a router. Multiple NICs and duplicate
CID handling across them remain application integration work.

Native tests cover independent wire fixtures (0/1/4/512 slots), malformed fields,
partial/oversized receive and recovery, membership/rebind failures, TX errors,
source selection/termination/expiry/sequence wrap, instance isolation, legacy gaps
and a deterministic 15,000-packet malformed sweep. CMake sACN ON/OFF and the Arduino
example matrix are CI checks. Hardware multicast routing, actual DMX/RDM timings
and controller interoperability require separate live qualification.
