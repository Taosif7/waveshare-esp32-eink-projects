#include "pond_proto.h"

#include <string.h>

namespace pond {

bool packet_valid(const Packet &packet) {
  if (packet.magic != kMagic || packet.version != kProtoVersion) {
    return false;
  }
  return packet.event >= static_cast<uint8_t>(Event::Handoff) &&
         packet.event <= static_cast<uint8_t>(Event::PassReset);
}

bool parse_packet(const uint8_t *data, size_t len, Packet *out) {
  if (data == nullptr || out == nullptr || len != sizeof(Packet)) {
    return false;
  }
  Packet packet;
  memcpy(&packet, data, sizeof(packet));
  if (!packet_valid(packet)) {
    return false;
  }
  *out = packet;
  return true;
}

bool dedup_accept(Dedup *dedup, const Packet &packet) {
  if (dedup == nullptr || !packet_valid(packet)) {
    return false;
  }
  if (dedup->have && dedup->seq == packet.seq && dedup->event == packet.event) {
    return false;
  }
  dedup->have = true;
  dedup->seq = packet.seq;
  dedup->event = packet.event;
  return true;
}

}  // namespace pond
