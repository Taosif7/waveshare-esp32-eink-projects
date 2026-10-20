#pragma once

// Prefer the repo-root .env (tools/load_wifi_env.py injects these at build time).
// Fallbacks below only apply if the pre-script was skipped.
#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
#endif
