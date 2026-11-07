#pragma once

#include <IPAddress.h>

bool web_begin();
void web_loop();
void web_broadcast(const char *message);
IPAddress web_ip();
