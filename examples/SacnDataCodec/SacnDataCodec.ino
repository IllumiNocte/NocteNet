// Protocol-only example: no network setup or physical DMX driver required.
#include <Arduino.h>
#include <NocteNetSacn.h>

nocte::net::SacnSourceSelector sources;
uint8_t packet[nocte::net::SACN_MAX_PACKET_SIZE];

void setup() {
  Serial.begin(115200);
  nocte::net::SacnSender sender{};
  sender.cid[0] = 0x42; // Demo only: assign your own persistent, unique CID.
  sender.name = "NocteNet sACN example";
  sender.universe = 1; sender.priority = 100; sender.sequence = 1;
  const uint8_t slots[] = {255, 0, 127, 42};
  const size_t length = nocte::net::encodeSacnData(packet, sizeof(packet), sender, slots, sizeof(slots));
  nocte::net::SacnData data{};
  if (nocte::net::decodeSacnData(packet, length, data)) {
    sources.accept(data, millis());
    const nocte::net::SacnSource* source = sources.selected();
    if (source) {
      Serial.print("Universe "); Serial.print(data.universe);
      Serial.print(", priority "); Serial.print(source->priority);
      Serial.print(", first slot "); Serial.println(source->frame[0]);
    }
  }
}

void loop() {
  sources.expire(millis());
  // Real network applications implement SacnTransport and call SacnNode::read().
  // Failsafe, source merge policy, DMX and interface setup remain application work.
}
