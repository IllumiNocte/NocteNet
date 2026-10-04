/*

Copyright (c) Charles Yarnold charlesyarnold@gmail.com 2015

Copyright (c) 2016-2020 Stephan Ruloff
https://github.com/rstephan/ArtnetnodeWifi

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

#include <ArtNetNode.h>


const char ArtNetNode::artnetId[] = ARTNET_ID;

ArtNetNode::ArtNetNode(nocte::net::UdpTransport& udp, nocte::net::Runtime& runtime)
  : Udp(udp), runtime_(runtime) {
  sequence = 1;
  physical = 0;
  incomingPhysical = 0;
  outgoingUniverse = 0;
  dmxDataLength = 0;
  packetSize = 0;
  oversizedPacketBytesRemaining = 0;
  discardUnreadPacketOnNextParse = false;
  opcode = 0;
  networkConfig = {};
  senderIp = nocte::net::IPv4();
  startingUniverse = 0;
  artDmxCallback = nullptr;
  artNzsCallback = nullptr;
  artSyncCallback = nullptr;
  artPollReplyCallback = nullptr;
  artAddressCallback = nullptr;
  artIpProgCallback = nullptr;
  artTodRequestCallback = nullptr;
  artTodControlCallback = nullptr;
  artRdmCallback = nullptr;

  for (uint8_t i = 0; i < MAX_PENDING_POLL_REPLIES; i++) {
    pendingPollReplies[i].active = false;
    pendingPollReplies[i].dueMillis = 0;
  }
}

/**
@retval 0 Ok
*/
uint8_t ArtNetNode::begin(const ArtNetNetworkConfig& network) {
  Udp.stop();
  oversizedPacketBytesRemaining = 0;
  // No management reply from a previous interface may survive a rebind,
  // including when the new socket cannot be opened.
  for (uint8_t i = 0; i < MAX_PENDING_POLL_REPLIES; i++) {
    pendingPollReplies[i].active = false;
  }
  if (!Udp.begin(ARTNET_PORT)) {
    return 1;
  }

  networkConfig = network;
  PollReplyPacket.setMac(networkConfig.mac);
  PollReplyPacket.setIP(networkConfig.ip);
  PollReplyPacket.setBindIP(networkConfig.ip);
  PollReplyPacket.canDHCP(true);
  PollReplyPacket.isDHCP(networkConfig.dhcp);
  PollReplyPacket.setWebConfig(true);

  for (uint8_t i = 0; i < MAX_PENDING_POLL_REPLIES; i++) {
    pendingPollReplies[i].active = false;
  }

  return 0;
}

void ArtNetNode::setShortName(const char name[]) {
  PollReplyPacket.setShortName(name);
}

void ArtNetNode::setLongName(const char name[]) {
  PollReplyPacket.setLongName(name);
}

void ArtNetNode::setName(const char name[]) {
  PollReplyPacket.setShortName(name);
  PollReplyPacket.setLongName(name);
}

void ArtNetNode::setNumPorts(uint8_t num) {
  PollReplyPacket.setNumPorts(num);
}

void ArtNetNode::setNodeReport(
  uint16_t code,
  const char* text) {
  PollReplyPacket.setNodeReport(
    code,
    text);
}

void ArtNetNode::setDirection(bool outputMode) {
  PollReplyPacket.clearPorts();

  if (outputMode) {
    PollReplyPacket.setOutputEnabled(0);
  } else {
    PollReplyPacket.setInputEnabled(0);
  }

  PollReplyPacket.setNumPorts(1);
}

void ArtNetNode::setPortInputActive(bool active) {
  PollReplyPacket.setInputDataActive(0, active);
}

void ArtNetNode::setPortOutputActive(bool active) {
  PollReplyPacket.setOutputDataActive(0, active);
}

void ArtNetNode::setPortOutputMergeStatus(
  bool active,
  bool ltpMode) {
  PollReplyPacket.setOutputMergeStatus(
    0,
    active,
    ltpMode);
}

void ArtNetNode::setFailsafeStatus(
  uint8_t mode,
  bool programmable) {
  PollReplyPacket.setFailsafeStatus(
    mode,
    programmable);
}

void ArtNetNode::setIndicatorState(
  ArtNetIndicatorState state) {
  PollReplyPacket.setIndicatorState(state);
}

ArtNetIndicatorState ArtNetNode::getIndicatorState() const {
  return PollReplyPacket.getIndicatorState();
}

void ArtNetNode::setStartingUniverse(uint16_t startUniverse) {
  startingUniverse = nocte::net::lesser(startUniverse, (uint16_t)0x7fff);
  PollReplyPacket.setStartingUniverse(startUniverse);
}

void ArtNetNode::setFirmwareVersion(uint8_t high, uint8_t low) {
  PollReplyPacket.setFirmwareVersion(high, low);
}

void ArtNetNode::setUniverse(uint16_t universe) {
  outgoingUniverse = nocte::net::lesser(universe, (uint16_t)0x7fff);
}

void ArtNetNode::setLength(uint16_t len) {
  dmxDataLength = nocte::net::lesser(len, (uint16_t)DMX_MAX_BUFFER);
}

uint16_t ArtNetNode::read() {
  uint8_t startcode;

  processPendingPollReplies();

  if (oversizedPacketBytesRemaining > 0) {
    discardOversizedPacketChunk();
    return 0;
  }

  const int parsedSize = Udp.parsePacket();

  if (parsedSize <= 0) {
    return 0;
  }

  if (parsedSize > ARTNET_MAX_BUFFER) {
    parserDiagnostics.oversizedPackets++;

    if (!discardUnreadPacketOnNextParse) {
      oversizedPacketBytesRemaining = parsedSize;
      discardOversizedPacketChunk();
    }

    return 0;
  }

  packetSize = static_cast<uint16_t>(parsedSize);

  const int bytesRead = Udp.read(artnetPacket, packetSize);
  if (bytesRead != packetSize) {
    parserDiagnostics.malformedPackets++;
    if (!discardUnreadPacketOnNextParse && bytesRead >= 0 && bytesRead < packetSize) {
      oversizedPacketBytesRemaining = packetSize - bytesRead;
    }
    return 0;
  }

  if (packetSize >= 10) {
    senderIp = Udp.remoteIP();

    // Check that packetID is "Art-Net" else ignore
    if (memcmp(artnetPacket, artnetId, sizeof(artnetId)) != 0) {
      parserDiagnostics.invalidIdPackets++;
      return 0;
    }

    opcode = artnetPacket[8] | artnetPacket[9] << 8;

    switch (opcode) {
      case OpDmx:
        if (packetSize < ARTNET_DMX_START_LOC) {
          parserDiagnostics.malformedPackets++;
          return 0;
        }
        if (!hasSupportedProtocolVersion()) {
          parserDiagnostics.unsupportedProtocolPackets++;
          return 0;
        }
        return handleDMX(0);
      case OpPoll:
        if (packetSize < ARTNET_POLL_MIN_LENGTH) {
          parserDiagnostics.malformedPackets++;
          return 0;
        }
        if (!hasSupportedProtocolVersion()) {
          parserDiagnostics.unsupportedProtocolPackets++;
          return 0;
        }
        if (!isTargetedPollForThisNode()) {
          return 0;
        }
        queuePollReply(senderIp);
        break;
      case OpNzs:
        if (packetSize < ARTNET_DMX_START_LOC) {
          parserDiagnostics.malformedPackets++;
          return 0;
        }
        if (!hasSupportedProtocolVersion()) {
          parserDiagnostics.unsupportedProtocolPackets++;
          return 0;
        }
        startcode = artnetPacket[13];
        if (startcode != 0 && startcode != DMX_RDM_STARTCODE) {
          return handleDMX(startcode);
        }
        break;
      case OpSync:
        if (packetSize < ARTNET_SYNC_MIN_LENGTH) {
          parserDiagnostics.malformedPackets++;
          return 0;
        }
        if (!hasSupportedProtocolVersion()) {
          parserDiagnostics.unsupportedProtocolPackets++;
          return 0;
        }
        if (artSyncCallback) {
          (*artSyncCallback)();
        }
        return OpSync;
      case OpPollReply:
        if (packetSize < ARTNET_POLL_REPLY_MIN_LENGTH) {
          parserDiagnostics.malformedPackets++;
          return 0;
        }
        return handlePollReply();

      case OpAddress:
        if (packetSize < ARTNET_ADDRESS_MIN_LENGTH) {
          parserDiagnostics.malformedPackets++;
          return 0;
        }
        if (!hasSupportedProtocolVersion()) {
          parserDiagnostics.unsupportedProtocolPackets++;
          return 0;
        }
        return handleArtAddress();

      case OpIpProg:
        if (packetSize < ARTNET_IP_PROG_MIN_LENGTH) {
          parserDiagnostics.malformedPackets++;
          return 0;
        }
        if (!hasSupportedProtocolVersion()) {
          parserDiagnostics.unsupportedProtocolPackets++;
          return 0;
        }
        return handleArtIpProg();

      case OpTodRequest:
        if (packetSize < ARTNET_TOD_REQUEST_LENGTH) {
          parserDiagnostics.malformedPackets++;
          return 0;
        }
        if (!hasSupportedProtocolVersion()) {
          parserDiagnostics.unsupportedProtocolPackets++;
          return 0;
        }
        return handleArtTodRequest();

      case OpTodData:
        if (packetSize < ARTNET_TOD_DATA_HEADER_LENGTH) {
          parserDiagnostics.malformedPackets++;
          return 0;
        }
        if (!hasSupportedProtocolVersion()) {
          parserDiagnostics.unsupportedProtocolPackets++;
          return 0;
        }
        // Output gateways take no action when receiving ArtTodData.
        return OpTodData;

      case OpTodControl:
        if (packetSize < ARTNET_TOD_CONTROL_LENGTH) {
          parserDiagnostics.malformedPackets++;
          return 0;
        }
        if (!hasSupportedProtocolVersion()) {
          parserDiagnostics.unsupportedProtocolPackets++;
          return 0;
        }
        return handleArtTodControl();

      case OpRdm:
        if (packetSize <= ARTNET_RDM_HEADER_LENGTH) {
          parserDiagnostics.malformedPackets++;
          return 0;
        }
        if (!hasSupportedProtocolVersion()) {
          parserDiagnostics.unsupportedProtocolPackets++;
          return 0;
        }
        return handleArtRdm();

      default:
        parserDiagnostics.unsupportedOpcodes++;
        break;
    }

    return opcode;
  }

  parserDiagnostics.shortPackets++;
  return 0;
}

void ArtNetNode::discardOversizedPacketChunk() {
  static const size_t DISCARD_CHUNK_SIZE = 256;
  uint8_t discard[DISCARD_CHUNK_SIZE];

  const size_t requested = nocte::net::lesser(
    (size_t)oversizedPacketBytesRemaining,
    DISCARD_CHUNK_SIZE);
  const int bytesRead = Udp.read(discard, requested);

  if (bytesRead <= 0) {
    // A transport may already have released the current datagram. Avoid
    // stalling all future packets when no more bytes can be consumed.
    oversizedPacketBytesRemaining = 0;
    return;
  }

  oversizedPacketBytesRemaining -= bytesRead;
}

bool ArtNetNode::hasSupportedProtocolVersion() const {
  if (packetSize < 12) {
    return false;
  }

  const uint16_t version =
    ((uint16_t)artnetPacket[10] << 8)
    | artnetPacket[11];

  return version >= ARTNET_PROTOCOL_VERSION;
}

bool ArtNetNode::isTargetedPollForThisNode() const {
  if ((artnetPacket[12] & 0x20) == 0) {
    return true;
  }

  const uint16_t top =
    packetSize >= 16
      ? ((uint16_t)artnetPacket[14] << 8) | artnetPacket[15]
      : 0;
  const uint16_t bottom =
    packetSize >= 18
      ? ((uint16_t)artnetPacket[16] << 8) | artnetPacket[17]
      : 0;

  return startingUniverse >= bottom
         && startingUniverse <= top;
}

uint16_t ArtNetNode::makePacket(void) {
  if (dmxDataLength < 2
      || dmxDataLength > DMX_MAX_BUFFER) {
    return 0;
  }

  memcpy(artnetPacket, artnetId, sizeof(artnetId));
  opcode = OpDmx;
  artnetPacket[8] = opcode & 0xff;
  artnetPacket[9] = opcode >> 8;
  artnetPacket[10] = ARTNET_PROTOCOL_VERSION >> 8;
  artnetPacket[11] = ARTNET_PROTOCOL_VERSION & 0xff;
  artnetPacket[12] = sequence;
  sequence++;
  if (!sequence) {
    sequence = 1;
  }
  artnetPacket[13] = physical;
  artnetPacket[14] = outgoingUniverse & 0xff;
  artnetPacket[15] = (outgoingUniverse >> 8) & 0x7f;

  uint16_t len = dmxDataLength;
  if (len & 1) {
    artnetPacket[ARTNET_DMX_START_LOC + len] = 0;
    len++;
  }

  artnetPacket[16] = len >> 8;
  artnetPacket[17] = len & 0xff;

  return len;
}

int ArtNetNode::write(nocte::net::IPv4 ip) {
  uint16_t len;

  len = makePacket();
  if (!len || !Udp.beginPacket(ip, ARTNET_PORT)) {
    return 0;
  }

  const size_t packetLength = ARTNET_DMX_START_LOC + len;
  if (Udp.write(artnetPacket, packetLength) != packetLength) {
    Udp.endPacket();
    return 0;
  }

  return Udp.endPacket();
}

uint8_t ArtNetNode::write(
  const nocte::net::IPv4 targets[],
  uint8_t targetCount) {
  if (!targets || targetCount == 0) {
    return 0;
  }

  const uint16_t len = makePacket();
  if (!len) {
    return 0;
  }

  const size_t packetLength = ARTNET_DMX_START_LOC + len;
  uint8_t sentCount = 0;

  for (uint8_t i = 0; i < targetCount; i++) {
    if (!Udp.beginPacket(targets[i], ARTNET_PORT)) {
      continue;
    }

    if (Udp.write(artnetPacket, packetLength) != packetLength) {
      Udp.endPacket();
      continue;
    }

    if (Udp.endPacket()) {
      sentCount++;
    }
  }

  return sentCount;
}

int ArtNetNode::sendArtPoll(nocte::net::IPv4 ip) {
  uint8_t pollPacket[14] = { 0 };

  memcpy(
    pollPacket,
    artnetId,
    sizeof(artnetId));

  pollPacket[8] = (uint8_t)(OpPoll & 0xff);
  pollPacket[9] = (uint8_t)(OpPoll >> 8);
  pollPacket[10] = ARTNET_PROTOCOL_VERSION >> 8;
  pollPacket[11] = ARTNET_PROTOCOL_VERSION & 0xff;
  pollPacket[12] = 0;
  pollPacket[13] = 0x10;

  if (!Udp.beginPacket(ip, ARTNET_PORT)) {
    return 0;
  }

  if (Udp.write(pollPacket, sizeof(pollPacket))
      != sizeof(pollPacket)) {
    Udp.endPacket();
    return 0;
  }

  return Udp.endPacket();
}

uint16_t ArtNetNode::sendArtTodData(
  nocte::net::IPv4 requester,
  uint16_t portAddress,
  const uint8_t* uids,
  uint16_t uidCount,
  bool available,
  uint8_t physicalPort) {
  if (portAddress > 0x7fff
      || physicalPort < 1
      || physicalPort > 4
      || uidCount > ARTNET_TOD_MAX_UIDS
      || (available && uidCount > 0 && !uids)) {
    return 0;
  }

  uint16_t packetCount = 1;
  if (available && uidCount > 0) {
    packetCount =
      (uidCount + ARTNET_TOD_UIDS_PER_PACKET - 1)
      / ARTNET_TOD_UIDS_PER_PACKET;
  }

  uint16_t packetsSent = 0;
  for (uint16_t block = 0; block < packetCount; block++) {
    uint8_t header[ARTNET_TOD_DATA_HEADER_LENGTH] = { 0 };
    memcpy(header, artnetId, sizeof(artnetId));
    header[8] = OpTodData & 0xff;
    header[9] = OpTodData >> 8;
    header[10] = ARTNET_PROTOCOL_VERSION >> 8;
    header[11] = ARTNET_PROTOCOL_VERSION & 0xff;
    header[12] = ARTNET_RDM_VERSION_STANDARD;
    header[13] = physicalPort;
    header[20] = PollReplyPacket.getBindIndex();
    header[21] = (portAddress >> 8) & 0x7f;
    header[22] = available ? ARTNET_TOD_FULL : ARTNET_TOD_NAK;
    header[23] = portAddress & 0xff;

    const uint16_t total = available ? uidCount : 0;
    header[24] = total >> 8;
    header[25] = total & 0xff;
    header[26] = block & 0xff;

    uint8_t blockUidCount = 0;
    if (available && uidCount > 0) {
      const uint16_t firstUid =
        block * ARTNET_TOD_UIDS_PER_PACKET;
      const uint16_t remaining = uidCount - firstUid;
      blockUidCount = static_cast<uint8_t>(nocte::net::lesser(
        remaining,
        (uint16_t)ARTNET_TOD_UIDS_PER_PACKET));
    }
    header[27] = blockUidCount;

    if (!Udp.beginPacket(requester, ARTNET_PORT)) {
      return packetsSent;
    }

    if (Udp.write(header, sizeof(header)) != sizeof(header)) {
      Udp.endPacket();
      return packetsSent;
    }

    if (blockUidCount > 0) {
      const size_t uidBytes =
        (size_t)blockUidCount * ARTNET_RDM_UID_LENGTH;
      const uint8_t* blockUids =
        uids
        + (size_t)block
          * ARTNET_TOD_UIDS_PER_PACKET
          * ARTNET_RDM_UID_LENGTH;
      if (Udp.write(blockUids, uidBytes) != uidBytes) {
        Udp.endPacket();
        return packetsSent;
      }
    }

    if (!Udp.endPacket()) {
      return packetsSent;
    }

    packetsSent++;
  }

  return packetsSent;
}

int ArtNetNode::sendArtRdm(
  nocte::net::IPv4 requester,
  uint16_t portAddress,
  const uint8_t* rdmData,
  uint16_t rdmLength,
  uint8_t fifoAvailable,
  uint8_t fifoMaximum) {
  if (portAddress > 0x7fff
      || !rdmData
      || rdmLength < 3
      || rdmLength > ARTNET_RDM_MAX_DATA_LENGTH
      || rdmLength != (uint16_t)rdmData[1] + 1) {
    return 0;
  }

  uint8_t header[ARTNET_RDM_HEADER_LENGTH] = { 0 };
  memcpy(header, artnetId, sizeof(artnetId));
  header[8] = OpRdm & 0xff;
  header[9] = OpRdm >> 8;
  header[10] = ARTNET_PROTOCOL_VERSION >> 8;
  header[11] = ARTNET_PROTOCOL_VERSION & 0xff;
  header[12] = ARTNET_RDM_VERSION_STANDARD;
  header[19] = fifoAvailable;
  header[20] = fifoMaximum;
  header[21] = (portAddress >> 8) & 0x7f;
  header[22] = ARTNET_RDM_COMMAND_PROCESS;
  header[23] = portAddress & 0xff;

  if (!Udp.beginPacket(requester, ARTNET_PORT)) {
    return 0;
  }

  if (Udp.write(header, sizeof(header)) != sizeof(header)
      || Udp.write(rdmData, rdmLength) != rdmLength) {
    Udp.endPacket();
    return 0;
  }

  return Udp.endPacket();
}

void ArtNetNode::setByte(uint16_t pos, uint8_t value) {
  if (pos >= 512) {
    return;
  }
  artnetPacket[ARTNET_DMX_START_LOC + pos] = value;
}


uint16_t ArtNetNode::handleDMX(uint8_t nzs) {
  // Get universe
  uint16_t universe = artnetPacket[14] | artnetPacket[15] << 8;

  // Get DMX frame length
  uint16_t incomingLength = artnetPacket[17] | artnetPacket[16] << 8;

  const uint16_t minimumLength = nzs ? 1 : 2;

  if (incomingLength < minimumLength
      || incomingLength > DMX_MAX_BUFFER
      || (!nzs && (incomingLength & 1))
      || incomingLength > packetSize - ARTNET_DMX_START_LOC) {
    parserDiagnostics.malformedPackets++;
    return 0;
  }

  // Sequence
  uint8_t incomingSequence = artnetPacket[12];
  incomingPhysical = artnetPacket[13];

  if (!nzs && artDmxCallback) {
    (*artDmxCallback)(universe, incomingLength, incomingSequence, artnetPacket + ARTNET_DMX_START_LOC);
  } else if (nzs && artNzsCallback) {
    (*artNzsCallback)(universe, incomingLength, incomingSequence, nzs, artnetPacket + ARTNET_DMX_START_LOC);
  }

  if (nzs) {
    return OpNzs;
  } else {
    return OpDmx;
  }
}

uint16_t ArtNetNode::sendPollReply(nocte::net::IPv4 requester) {

  artPollCounter++;
  lastPollMillis = runtime_.nowMillis();
  if (!Udp.beginPacket(requester, ARTNET_PORT)) {
    return 0;
  }

  const size_t replyLength = PollReplyPacket.size();
  if (Udp.write(PollReplyPacket.printPacket(), replyLength)
      != replyLength) {
    Udp.endPacket();
    return 0;
  }

  if (!Udp.endPacket()) {
    return 0;
  }

  return OpPoll;
}

void ArtNetNode::queuePollReply(nocte::net::IPv4 requester) {
  for (uint8_t i = 0; i < MAX_PENDING_POLL_REPLIES; i++) {
    if (pendingPollReplies[i].active
        && pendingPollReplies[i].requester == requester) {
      return;
    }
  }

  for (uint8_t i = 0; i < MAX_PENDING_POLL_REPLIES; i++) {
    if (!pendingPollReplies[i].active) {
      pendingPollReplies[i].requester = requester;
      pendingPollReplies[i].dueMillis = runtime_.nowMillis() + runtime_.randomBelow(1000);
      pendingPollReplies[i].active = true;
      return;
    }
  }
}

void ArtNetNode::processPendingPollReplies() {
  const uint32_t now = runtime_.nowMillis();

  for (uint8_t i = 0; i < MAX_PENDING_POLL_REPLIES; i++) {
    if (pendingPollReplies[i].active
        && (int32_t)(now - pendingPollReplies[i].dueMillis) >= 0) {
      const nocte::net::IPv4 requester = pendingPollReplies[i].requester;
      pendingPollReplies[i].active = false;
      sendPollReply(requester);
    }
  }
}

uint16_t ArtNetNode::handlePollReply() {
  ArtPollReplyInfo info;

  info.senderIP = senderIp;
  info.reportedIP = nocte::net::IPv4(
    artnetPacket[10],
    artnetPacket[11],
    artnetPacket[12],
    artnetPacket[13]);
  memcpy(info.portName, artnetPacket + 26, 18);
  info.portName[18] = '\0';
  info.netSwitch = artnetPacket[18] & 0x7f;
  info.subSwitch = artnetPacket[19] & 0x0f;
  info.numPorts = static_cast<uint8_t>(nocte::net::lesser(
    (uint16_t)(((uint16_t)artnetPacket[172] << 8)
               | artnetPacket[173]),
    (uint16_t)4));
  memcpy(info.portTypes, artnetPacket + 174, 4);
  memcpy(info.swIn, artnetPacket + 186, 4);
  memcpy(info.swOut, artnetPacket + 190, 4);
  info.bindIndex =
    packetSize > 211
      ? artnetPacket[211]
      : 0;

  if (artPollReplyCallback) {
    (*artPollReplyCallback)(info);
  }

  return OpPollReply;
}

uint16_t ArtNetNode::handleArtAddress() {
  const uint8_t bindIndex = artnetPacket[13];

  if (bindIndex != PollReplyPacket.getBindIndex()) {
    return 0;
  }

  ArtAddressInfo info;
  info.senderIP = senderIp;
  info.netSwitch = artnetPacket[12];
  info.bindIndex = bindIndex;
  memcpy(info.portName, artnetPacket + 14, 18);
  info.portName[18] = '\0';
  memcpy(info.longName, artnetPacket + 32, 64);
  info.longName[64] = '\0';
  memcpy(info.swIn, artnetPacket + 96, 4);
  memcpy(info.swOut, artnetPacket + 100, 4);
  info.subSwitch = artnetPacket[104];
  info.acnPriority = artnetPacket[105];
  info.command = artnetPacket[106];

  if (artAddressCallback) {
    (*artAddressCallback)(info);
  }

  sendPollReply(senderIp);
  return OpAddress;
}

uint16_t ArtNetNode::handleArtTodRequest() {
  const uint8_t addressCount = artnetPacket[23];
  if (addressCount > ARTNET_TOD_MAX_REQUEST_ADDRESSES
      || artnetPacket[22] != ARTNET_TOD_FULL) {
    parserDiagnostics.malformedPackets++;
    return 0;
  }

  ArtTodRequestInfo info;
  info.senderIP = senderIp;
  info.net = artnetPacket[21] & 0x7f;
  info.command = artnetPacket[22];
  info.addressCount = addressCount;
  memset(info.addresses, 0, sizeof(info.addresses));
  memcpy(
    info.addresses,
    artnetPacket + 24,
    addressCount);

  if (artTodRequestCallback) {
    (*artTodRequestCallback)(info);
  }

  return OpTodRequest;
}

uint16_t ArtNetNode::handleArtTodControl() {
  ArtTodControlInfo info;
  info.senderIP = senderIp;
  info.portAddress =
    ((uint16_t)(artnetPacket[21] & 0x7f) << 8)
    | artnetPacket[23];
  info.command = artnetPacket[22];

  if (artTodControlCallback) {
    (*artTodControlCallback)(info);
  }

  return OpTodControl;
}

uint16_t ArtNetNode::handleArtRdm() {
  const uint16_t rdmLength =
    packetSize - ARTNET_RDM_HEADER_LENGTH;
  const uint8_t* rdmData =
    artnetPacket + ARTNET_RDM_HEADER_LENGTH;

  if (artnetPacket[22] != ARTNET_RDM_COMMAND_PROCESS
      || rdmLength < 3
      || rdmLength > ARTNET_RDM_MAX_DATA_LENGTH
      || rdmLength != (uint16_t)rdmData[1] + 1) {
    parserDiagnostics.malformedPackets++;
    return 0;
  }

  ArtRdmInfo info;
  info.senderIP = senderIp;
  info.portAddress =
    ((uint16_t)(artnetPacket[21] & 0x7f) << 8)
    | artnetPacket[23];
  info.rdmVersion = artnetPacket[12];
  info.fifoAvailable = artnetPacket[19];
  info.fifoMaximum = artnetPacket[20];
  info.command = artnetPacket[22];
  info.rdmData = rdmData;
  info.rdmLength = rdmLength;

  if (artRdmCallback) {
    (*artRdmCallback)(info);
  }

  return OpRdm;
}

void ArtNetNode::getCurrentIpProgReplyInfo(
  ArtIpProgReplyInfo& info) const {
  info.ip = networkConfig.ip;
  info.subnet = networkConfig.subnet;
  info.gateway = networkConfig.gateway;
  info.port = ARTNET_PORT;
  info.dhcp = networkConfig.dhcp;
}

uint16_t ArtNetNode::sendIpProgReply(
  nocte::net::IPv4 requester,
  const ArtIpProgReplyInfo& info) {
  uint8_t reply[ARTNET_IP_PROG_REPLY_LENGTH] = { 0 };

  memcpy(reply, artnetId, sizeof(artnetId));
  reply[8] = OpIpProgReply & 0xff;
  reply[9] = OpIpProgReply >> 8;
  reply[10] = ARTNET_PROTOCOL_VERSION >> 8;
  reply[11] = ARTNET_PROTOCOL_VERSION & 0xff;

  for (uint8_t i = 0; i < 4; i++) {
    reply[16 + i] = info.ip[i];
    reply[20 + i] = info.subnet[i];
    reply[28 + i] = info.gateway[i];
  }

  reply[24] = info.port >> 8;
  reply[25] = info.port & 0xff;

  if (info.dhcp) {
    reply[26] = 0x40;
  }

  if (!Udp.beginPacket(requester, ARTNET_PORT)) {
    return 0;
  }

  if (Udp.write(reply, sizeof(reply)) != sizeof(reply)) {
    Udp.endPacket();
    return 0;
  }

  if (!Udp.endPacket()) {
    return 0;
  }

  return OpIpProgReply;
}

uint16_t ArtNetNode::handleArtIpProg() {
  ArtIpProgInfo info;
  ArtIpProgReplyInfo reply;

  info.senderIP = senderIp;
  info.command = artnetPacket[14];
  info.ip = nocte::net::IPv4(
    artnetPacket[16],
    artnetPacket[17],
    artnetPacket[18],
    artnetPacket[19]);
  info.subnet = nocte::net::IPv4(
    artnetPacket[20],
    artnetPacket[21],
    artnetPacket[22],
    artnetPacket[23]);
  info.port =
    ((uint16_t)artnetPacket[24] << 8)
    | artnetPacket[25];

  if (packetSize >= 30) {
    info.gateway = nocte::net::IPv4(
      artnetPacket[26],
      artnetPacket[27],
      artnetPacket[28],
      artnetPacket[29]);
  } else {
    info.gateway = nocte::net::IPv4();
  }

  getCurrentIpProgReplyInfo(reply);

  if (artIpProgCallback) {
    (*artIpProgCallback)(
      info,
      reply);
  }

  sendIpProgReply(
    senderIp,
    reply);

  return OpIpProg;
}
