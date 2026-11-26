#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pond_config.h"

namespace pond {

enum class Event : uint8_t {
  Handoff = 1,
  CycleDone = 2,
  CalOn = 3,
  CalOff = 4,
  PassReset = 5,
};

#pragma pack(push, 1)
struct Packet {
  uint32_t magic;
  uint8_t version;
  uint8_t event;
  uint8_t seq;
  uint8_t reserved;
  float y_mm;
  float phase;
  float speed_mm_s;
  float x_nose_mm;
  uint32_t loop_index;
};
#pragma pack(pop)

static_assert(sizeof(Packet) == 28, "ESP-NOW koi packet layout");
static_assert(sizeof(Packet) <= 250, "ESP-NOW payload limit");

struct Dedup {
  bool have;
  uint8_t seq;
  uint8_t event;
};

bool packet_valid(const Packet &packet);
bool parse_packet(const uint8_t *data, size_t len, Packet *out);
bool dedup_accept(Dedup *dedup, const Packet &packet);

}  // namespace pond
