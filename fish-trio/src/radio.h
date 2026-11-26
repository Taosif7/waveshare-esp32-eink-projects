#pragma once

#include <Arduino.h>

#include "pond_proto.h"

bool radio_begin();
uint8_t radio_take_seq();
void radio_send(const pond::Packet &packet);
void radio_poll(void (*on_packet)(const pond::Packet &packet));
