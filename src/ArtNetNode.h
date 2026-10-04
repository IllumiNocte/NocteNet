#ifndef ARTNETNODE_H
#define ARTNETNODE_H
/*

Copyright (c) Charles Yarnold charlesyarnold@gmail.com 2015

Copyright (c) 2016 Stephan Ruloff
https://github.com/rstephan

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, under version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "NocteNetTransport.h"
#include <string.h>
#include <stdio.h>
#include "OpCodes.h"
#include "NodeReportCodes.h"
#include "StyleCodes.h"
#include "PriorityCodes.h"
#include "ProtocolSettings.h"
#include "PollReply.h"

struct ArtPollReplyInfo {
  nocte::net::IPv4 senderIP;
  nocte::net::IPv4 reportedIP;
  char portName[19];
  uint8_t bindIndex;
  uint8_t netSwitch;
  uint8_t subSwitch;
  uint8_t numPorts;
  uint8_t portTypes[4];
  uint8_t swIn[4];
  uint8_t swOut[4];
};

struct ArtAddressInfo {
  nocte::net::IPv4 senderIP;
  uint8_t netSwitch;
  uint8_t bindIndex;
  char portName[19];
  char longName[65];
  uint8_t swIn[4];
  uint8_t swOut[4];
  uint8_t subSwitch;
  uint8_t acnPriority;
  uint8_t command;
};

struct ArtIpProgInfo {
  nocte::net::IPv4 senderIP;
  uint8_t command;
  nocte::net::IPv4 ip;
  nocte::net::IPv4 subnet;
  nocte::net::IPv4 gateway;
  uint16_t port;
};

struct ArtIpProgReplyInfo {
  nocte::net::IPv4 ip;
  nocte::net::IPv4 subnet;
  nocte::net::IPv4 gateway;
  uint16_t port;
  bool dhcp;
};

/** @brief Parsed ArtTodRequest for one Net and up to 32 Port-Addresses. */
struct ArtTodRequestInfo {
  nocte::net::IPv4 senderIP;
  uint8_t net;
  uint8_t command;
  uint8_t addressCount;
  uint8_t addresses[ARTNET_TOD_MAX_REQUEST_ADDRESSES];
};

/** @brief Parsed ArtTodControl command for one 15-bit Port-Address. */
struct ArtTodControlInfo {
  nocte::net::IPv4 senderIP;
  uint16_t portAddress;
  uint8_t command;
};

/**
 * @brief Parsed ArtRdm message.
 *
 * `rdmData` points into the internal receive buffer and is valid only for the
 * duration of the callback. It excludes the DMX start code as required by
 * Art-Net.
 */
struct ArtRdmInfo {
  nocte::net::IPv4 senderIP;
  uint16_t portAddress;
  uint8_t rdmVersion;
  uint8_t fifoAvailable;
  uint8_t fifoMaximum;
  uint8_t command;
  const uint8_t* rdmData;
  uint16_t rdmLength;
};

struct ArtNetParserDiagnostics {
  uint32_t oversizedPackets;
  uint32_t shortPackets;
  uint32_t invalidIdPackets;
  uint32_t unsupportedProtocolPackets;
  uint32_t malformedPackets;
  uint32_t unsupportedOpcodes;
};

/** @brief Network identity supplied by the application hosting Art-Net. */
struct ArtNetNetworkConfig {
  nocte::net::IPv4 ip;
  nocte::net::IPv4 subnet;
  nocte::net::IPv4 gateway;
  uint8_t mac[6];
  bool dhcp;
};

class ArtNetNode {
public:
  /** @brief Initializes protocol state using an platform-neutral UDP transport and runtime. */
  ArtNetNode(nocte::net::UdpTransport& udp, nocte::net::Runtime& runtime);

  /** @brief Binds the Art-Net UDP socket and applies the active interface identity. */
  uint8_t begin(const ArtNetNetworkConfig& network);
  /** @brief Processes one pending Art-Net datagram and deferred replies. */
  uint16_t read();

  // Node identity
  /** @brief Sets the advertised Art-Net Port Name. */
  void setShortName(const char name[]);
  /** @brief Sets the advertised Art-Net Long Name. */
  void setLongName(const char name[]);
  /** @brief Sets both advertised names to the same value. */
  void setName(const char name[]);
  /** @brief Sets the advertised number of ports. */
  void setNumPorts(uint8_t num);
  /** @brief Sets the ArtPollReply NodeReport status. */
  void setNodeReport(
    uint16_t code,
    const char* text);
  /** @brief Sets the starting 15-bit Port-Address. */
  void setStartingUniverse(uint16_t startingUniverse);
  /** @brief Sets the advertised firmware version bytes. */
  void setFirmwareVersion(uint8_t high, uint8_t low);

  // Transmit
  /** @return UDP result for ArtDmx sent to one IP address. */
  int write(nocte::net::IPv4 ip);
  /** @return Number of IP targets that accepted the same ArtDmx frame. */
  uint8_t write(
    const nocte::net::IPv4 targets[],
    uint8_t targetCount);
  /** @return UDP result for an ArtPoll sent to the given IP address. */
  int sendArtPoll(nocte::net::IPv4 ip);
  /** @brief Sets one zero-based byte in the outgoing ArtDmx payload. */
  void setByte(uint16_t pos, uint8_t value);
  /** @brief Sets the outgoing ArtDmx Port-Address. */
  void setUniverse(uint16_t universe);

  /** @brief Sets the ArtDmx physical input port field. */
  inline void setPhysical(uint8_t port) {
    physical = port;
  }

  /** @brief Sets outgoing ArtDmx payload length, clamped to 512. */
  void setLength(uint16_t len);

  /** @brief Sets one raw ArtPollReply PortTypes entry. */
  inline void setPortType(uint8_t port, uint8_t type) {
    PollReplyPacket.setPortType(port, type);
  }

  /** @brief Updates the advertised DHCP-capable flag. */
  inline void canDHCP(bool can) {
    PollReplyPacket.canDHCP(can);
  }

  /** @brief Updates the advertised DHCP-configured flag. */
  inline void isDHCP(bool is) {
    PollReplyPacket.isDHCP(is);
  }

  /** @brief Enables a conservative Art-Net 3 compatible ArtPollReply profile. */
  inline void setLegacyArtNet3Mode(bool enabled) {
    PollReplyPacket.setLegacyArtNet3Mode(enabled);
  }

  // DMX controls
  /** @brief Configures the single advertised port direction. */
  void setDirection(bool outputMode);
  /** @brief Updates physical DMX input activity status. */
  void setPortInputActive(bool active);
  /** @brief Updates physical DMX output activity status. */
  void setPortOutputActive(bool active);
  /** @brief Updates physical DMX output merge status. */
  void setPortOutputMergeStatus(
    bool active,
    bool ltpMode);
  /** @brief Updates ArtPollReply output-failsafe Status3 bits. */
  void setFailsafeStatus(
    uint8_t mode,
    bool programmable);
  /** @brief Updates ArtPollReply indicator state. */
  void setIndicatorState(ArtNetIndicatorState state);
  /** @return Current ArtPollReply indicator state. */
  ArtNetIndicatorState getIndicatorState() const;

  // RDM status and transport
  /** @brief Advertises whether this node is capable of RDM. */
  inline void setRdmCapable(bool capable) {
    PollReplyPacket.setRdmCapable(capable);
  }
  /** @brief Advertises whether ArtAddress may enable or disable RDM. */
  inline void setRdmArtAddressControl(bool supported) {
    PollReplyPacket.setRdmArtAddressControl(supported);
  }
  /** @brief Updates the RDM enabled state for one zero-based output port. */
  inline void setRdmEnabled(uint8_t port, bool enabled) {
    PollReplyPacket.setRdmEnabled(port, enabled);
  }
  /** @brief Updates the discovery-running state for one output port. */
  inline void setRdmDiscoveryRunning(uint8_t port, bool running) {
    PollReplyPacket.setRdmDiscoveryRunning(port, running);
  }
  /** @brief Updates background discovery for one output port. */
  inline void setRdmBackgroundDiscovery(uint8_t port, bool enabled) {
    PollReplyPacket.setRdmBackgroundDiscovery(port, enabled);
  }

  /**
   * @brief Unicasts a complete TOD, split into blocks of at most 200 UIDs.
   *
   * @param requester Destination controller address.
   * @param portAddress 15-bit Art-Net Port-Address.
   * @param uids Flat array containing `uidCount * 6` bytes.
   * @param uidCount Total UID count across all blocks.
   * @param available False sends one TodNak response.
   * @param physicalPort One-based Art-Net physical port index, range 1-4.
   * @return Number of ArtTodData packets transmitted successfully.
   */
  uint16_t sendArtTodData(
    nocte::net::IPv4 requester,
    uint16_t portAddress,
    const uint8_t* uids,
    uint16_t uidCount,
    bool available = true,
    uint8_t physicalPort = 1);

  /**
   * @brief Unicasts one ArtRdm message with the DMX start code omitted.
   * @return UDP transport result, or zero when arguments are invalid.
   */
  int sendArtRdm(
    nocte::net::IPv4 requester,
    uint16_t portAddress,
    const uint8_t* rdmData,
    uint16_t rdmLength,
    uint8_t fifoAvailable = 0,
    uint8_t fifoMaximum = 0);

  // Return a pointer to the start of the DMX data
  /** @return Pointer to the current incoming ArtDmx payload buffer. */
  inline uint8_t* getDmxFrame(void) {
    return artnetPacket + ARTNET_DMX_START_LOC;
  }

  /** @brief Registers the zero-start-code ArtDmx callback. */
  inline void setArtDmxCallback(void (*fptr)(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data)) {
    artDmxCallback = fptr;
  }

  /** @brief Registers the non-zero-start-code ArtNzs callback. */
  inline void setArtNzsCallback(void (*fptr)(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t startCode, uint8_t* data)) {
    artNzsCallback = fptr;
  }

  /** @brief Registers the ArtSync callback. */
  inline void setArtSyncCallback(void (*fptr)()) {
    artSyncCallback = fptr;
  }

  /** @brief Registers the parsed ArtPollReply callback. */
  inline void setArtPollReplyCallback(
      void (*fptr)(const ArtPollReplyInfo& info)) {
    artPollReplyCallback = fptr;
  }

  /** @brief Registers the parsed ArtAddress callback. */
  inline void setArtAddressCallback(
      void (*fptr)(const ArtAddressInfo& info)) {
    artAddressCallback = fptr;
  }

  /** @brief Registers the parsed ArtIpProg callback. */
  inline void setArtIpProgCallback(
      bool (*fptr)(const ArtIpProgInfo& info,
                   ArtIpProgReplyInfo& reply)) {
    artIpProgCallback = fptr;
  }

  /** @brief Registers the parsed ArtTodRequest callback. */
  inline void setArtTodRequestCallback(
      void (*fptr)(const ArtTodRequestInfo& info)) {
    artTodRequestCallback = fptr;
  }

  /** @brief Registers the parsed ArtTodControl callback. */
  inline void setArtTodControlCallback(
      void (*fptr)(const ArtTodControlInfo& info)) {
    artTodControlCallback = fptr;
  }

  /** @brief Registers the parsed ArtRdm callback. */
  inline void setArtRdmCallback(
      void (*fptr)(const ArtRdmInfo& info)) {
    artRdmCallback = fptr;
  }

  /** @return IP address that sent the most recently parsed datagram. */
  inline nocte::net::IPv4& getSenderIp(void) {
    return senderIp;
  }

  /** @return ArtDmx Physical field from the most recently parsed datagram. */
  inline uint8_t getIncomingPhysical(void) const {
    return incomingPhysical;
  }

  /** @return Number of attempted ArtPollReply transmissions. */
  inline uint32_t getPollCount() {
    return artPollCounter;
  }

  /** @return Current starting 15-bit Port-Address. */
  inline uint16_t getStartingUniverse() const {
    return startingUniverse;
  }

  /** @brief Selects Locate or Normal indicator state. */
  inline void setSquawking(bool enabled) {
    PollReplyPacket.setSquawking(enabled);
  }

  /** @return True when Locate/squawking state is active. */
  inline bool isSquawking(void) {
    return PollReplyPacket.isSquawking();
  }

  /** @return monotonic millisecond timestamp of the last ArtPollReply attempt. */
  inline uint32_t getLastPollMillis() {
    return lastPollMillis;
  }

  /** @return Low-level Art-Net parser diagnostic counters. */
  inline const ArtNetParserDiagnostics& getParserDiagnostics() const {
    return parserDiagnostics;
  }

  /**
   * @brief Selects an O(1) discard path for capable UDP transports.
   *
   * Enable this only when the transport guarantees that parsePacket()
   * releases any unread payload from the preceding datagram. Unknown
   * transports use the portable bounded-discard state machine by default.
   */
  inline void setDiscardUnreadPacketOnNextParse(bool supported) {
    discardUnreadPacketOnNextParse = supported;
  }

  static const char artnetId[];

private:
  nocte::net::UdpTransport& Udp;
  nocte::net::Runtime& runtime_;
  PollReply PollReplyPacket;
  nocte::net::IPv4 senderIp;
  ArtNetNetworkConfig networkConfig;

  // Packet handlers
  /** @brief Validates and dispatches an ArtDmx or ArtNzs payload. */
  uint16_t handleDMX(uint8_t nzs);
  /** @brief Sends an immediate unicast ArtPollReply. */
  uint16_t sendPollReply(nocte::net::IPv4 requester);
  /** @brief Parses an ArtPollReply into the public callback structure. */
  uint16_t handlePollReply();
  /** @brief Parses ArtAddress, invokes the callback, and replies. */
  uint16_t handleArtAddress();
  /** @brief Parses ArtIpProg, invokes the callback, and replies. */
  uint16_t handleArtIpProg();
  /** @brief Parses an ArtTodRequest and invokes the application callback. */
  uint16_t handleArtTodRequest();
  /** @brief Parses an ArtTodControl and invokes the application callback. */
  uint16_t handleArtTodControl();
  /** @brief Parses an ArtRdm payload and invokes the application callback. */
  uint16_t handleArtRdm();
  /** @brief Sends an ArtIpProgReply to one controller. */
  uint16_t sendIpProgReply(
    nocte::net::IPv4 requester,
    const ArtIpProgReplyInfo& info);
  /** @brief Populates current network settings for ArtIpProgReply. */
  void getCurrentIpProgReplyInfo(
    ArtIpProgReplyInfo& info) const;
  /** @brief Adds a requester to the delayed ArtPollReply queue. */
  void queuePollReply(nocte::net::IPv4 requester);
  /** @brief Sends delayed ArtPollReply entries whose deadlines elapsed. */
  void processPendingPollReplies();
  /** @brief Discards one bounded chunk of the current oversized UDP packet. */
  void discardOversizedPacketChunk();
  /** @return True when the current packet declares protocol version 14+. */
  bool hasSupportedProtocolVersion() const;
  /** @return True when a targeted ArtPoll includes this node. */
  bool isTargetedPollForThisNode() const;

  // Packet vars
  uint8_t artnetPacket[ARTNET_MAX_BUFFER];
  uint16_t packetSize;
  uint16_t opcode;
  uint8_t sequence;
  uint8_t physical;
  uint8_t incomingPhysical;
  uint16_t outgoingUniverse;
  uint16_t dmxDataLength;
  int oversizedPacketBytesRemaining;
  bool discardUnreadPacketOnNextParse;
  // Packet functions
  /** @return Even ArtDmx payload length, or zero when configuration is invalid. */
  uint16_t makePacket(void);

  struct PendingPollReply {
    nocte::net::IPv4 requester;
    uint32_t dueMillis;
    bool active;
  };

  static const uint8_t MAX_PENDING_POLL_REPLIES = 4;
  PendingPollReply pendingPollReplies[MAX_PENDING_POLL_REPLIES];

  uint16_t startingUniverse;

  uint32_t artPollCounter = 0;
  uint32_t lastPollMillis = 0;
  ArtNetParserDiagnostics parserDiagnostics = {};

  void (*artDmxCallback)(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data);
  void (*artNzsCallback)(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t startCode, uint8_t* data);
  void (*artSyncCallback)();
  void (*artPollReplyCallback)(const ArtPollReplyInfo& info);
  void (*artAddressCallback)(const ArtAddressInfo& info);
  bool (*artIpProgCallback)(const ArtIpProgInfo& info,
                            ArtIpProgReplyInfo& reply);
  void (*artTodRequestCallback)(const ArtTodRequestInfo& info);
  void (*artTodControlCallback)(const ArtTodControlInfo& info);
  void (*artRdmCallback)(const ArtRdmInfo& info);
};

#endif
