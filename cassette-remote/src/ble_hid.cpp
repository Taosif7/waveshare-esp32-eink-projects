#include "ble_hid.h"

#include <Arduino.h>

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

namespace {

// One byte, 7 consumer bits plus a constant pad bit so the report is aligned.
//   0 Scan Previous Track  0xB6
//   1 Rewind               0xB4
//   2 Play/Pause           0xCD
//   3 Fast Forward         0xB3
//   4 Scan Next Track      0xB5
//   5 Volume Increment     0xE9
//   6 Volume Decrement     0xEA
constexpr uint8_t kReportMap[] = {
    0x05, 0x0C,  // Usage Page (Consumer)
    0x09, 0x01,  // Usage (Consumer Control)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  //   Report ID (1)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x07,  //   Report Count (7)
    0x09, 0xB6,  //   Scan Previous Track
    0x09, 0xB4,  //   Rewind
    0x09, 0xCD,  //   Play/Pause
    0x09, 0xB3,  //   Fast Forward
    0x09, 0xB5,  //   Scan Next Track
    0x09, 0xE9,  //   Volume Increment
    0x09, 0xEA,  //   Volume Decrement
    0x81, 0x02,  //   Input (Data,Var,Abs)
    0x95, 0x01,  //   Report Count (1)
    0x81, 0x03,  //   Input (Const)
    0xC0,        // End Collection
};

// GAP appearance: Generic Remote Control.
constexpr uint16_t kAppearanceRemote = 0x0180;

NimBLEHIDDevice *hid = nullptr;
NimBLECharacteristic *inputReport = nullptr;

volatile bool gConnected = false;
volatile bool gEncrypted = false;
volatile bool gSubscribed = false;
volatile bool gSendZero = false;
volatile bool gParamsPending = false;
volatile uint16_t gHandle = 0;
volatile bool gLogConnect = false;
volatile bool gLogDisconnect = false;
volatile bool gLogAuthFail = false;
volatile int gLogReason = 0;

uint32_t gUntil[7] = {};
uint8_t gMask = 0;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server, NimBLEConnInfo &info) override {
    (void)server;
    gHandle = info.getConnHandle();
    gConnected = true;
    gEncrypted = info.isEncrypted();
    gParamsPending = true;
    gLogConnect = true;
    if (!gEncrypted) {
      NimBLEDevice::startSecurity(info.getConnHandle());
    }
  }

  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &info, int reason) override {
    (void)server;
    (void)info;
    gConnected = false;
    gEncrypted = false;
    gSubscribed = false;
    gSendZero = true;
    gLogReason = reason;
    gLogDisconnect = true;
  }

  void onAuthenticationComplete(NimBLEConnInfo &info) override {
    if (!info.isEncrypted()) {
      gEncrypted = false;
      gLogAuthFail = true;
      NimBLEDevice::getServer()->disconnect(info.getConnHandle());
      return;
    }
    gEncrypted = true;
    gHandle = info.getConnHandle();
    gParamsPending = true;
  }
};

class ReportCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *characteristic, NimBLEConnInfo &info, uint16_t subValue) override {
    (void)characteristic;
    (void)info;
    gSubscribed = (subValue & 0x0001) != 0;
    if (gSubscribed) {
      gSendZero = true;
    }
  }
};

ServerCallbacks serverCallbacks;
ReportCallbacks reportCallbacks;

void start_advertising() {
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  if (advertising == nullptr || advertising->isAdvertising()) {
    return;
  }
  advertising->start();
}

bool send_report(uint8_t mask) {
  if (!gConnected || !gSubscribed || inputReport == nullptr) {
    return false;
  }
  const uint8_t value = mask;
  return inputReport->notify(&value, 1);
}

}  // namespace

void ble_hid_begin() {
  if (!NimBLEDevice::init("Cassette")) {
    Serial.println("BLE init failed");
    return;
  }
  // Public address stays the factory MAC, so a bonded phone can find us again.
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
  // Just Works + bonding + LE secure connections. No passkey: the panel is not
  // a confirmation display, and phones already ask before they pair.
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setPower(9);

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks, false);
  server->advertiseOnDisconnect(true);

  hid = new NimBLEHIDDevice(server);
  inputReport = hid->getInputReport(1);
  inputReport->setCallbacks(&reportCallbacks);
  hid->setManufacturer("Cassette");
  hid->setPnp(0x02, 0xFFFF, 0x00C4, 0x0100);
  hid->setHidInfo(0x00, 0x02);
  hid->setReportMap(const_cast<uint8_t *>(kReportMap), sizeof(kReportMap));
  hid->setBatteryLevel(100);
  // NimBLE 2.5 starts GATT from the server. Advertising also calls this, and
  // a second call is a no-op once the services are up.
  if (!server->start()) {
    Serial.println("BLE GATT start failed");
    return;
  }

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->setName("Cassette");
  advertising->setAppearance(kAppearanceRemote);
  advertising->addServiceUUID(hid->getHidService()->getUUID());
  advertising->enableScanResponse(true);
  advertising->setPreferredParams(12, 24);
  advertising->setMinInterval(40);
  advertising->setMaxInterval(80);
  const bool started = advertising->start();
  Serial.printf("BLE advertising %s, stored bonds %d\n", started ? "on" : "FAILED",
                NimBLEDevice::getNumBonds());
}

void ble_hid_pulse(uint8_t bit, uint32_t nowMs, uint32_t durationMs) {
  const uint32_t until = nowMs + durationMs;
  for (int i = 0; i < 7; ++i) {
    if ((bit & (1u << i)) == 0) {
      continue;
    }
    if ((int32_t)(until - gUntil[i]) > 0) {
      gUntil[i] = until;
    }
  }
}

void ble_hid_service(uint32_t nowMs) {
  if (gLogConnect) {
    gLogConnect = false;
    Serial.printf("BLE connected, encrypted=%d bonds=%d\n", gEncrypted ? 1 : 0, NimBLEDevice::getNumBonds());
  }
  if (gLogAuthFail) {
    gLogAuthFail = false;
    Serial.println("BLE pairing failed");
  }
  if (gLogDisconnect) {
    gLogDisconnect = false;
    Serial.printf("BLE disconnected reason=%d\n", gLogReason);
    start_advertising();
  }

  if (gParamsPending && gEncrypted && gConnected) {
    gParamsPending = false;
    NimBLEServer *server = NimBLEDevice::getServer();
    if (server != nullptr) {
      server->updateConnParams(gHandle, 12, 24, 0, 400);
    }
  }

  if (gSendZero) {
    gSendZero = false;
    for (int i = 0; i < 7; ++i) {
      gUntil[i] = nowMs;
    }
    gMask = 0xFF;
  }

  uint8_t mask = 0;
  for (int i = 0; i < 7; ++i) {
    if ((int32_t)(gUntil[i] - nowMs) > 0) {
      mask |= (uint8_t)(1u << i);
    }
  }
  if (mask == gMask) {
    return;
  }
  gMask = mask;
  send_report(mask);
}

bool ble_hid_connected() {
  return gConnected;
}

void ble_hid_forget() {
  NimBLEServer *server = NimBLEDevice::getServer();
  if (server != nullptr && server->getConnectedCount() > 0) {
    server->disconnect(gHandle);
  }
  NimBLEDevice::deleteAllBonds();
  gConnected = false;
  gEncrypted = false;
  gSubscribed = false;
  gSendZero = true;
  Serial.println("BLE bonds cleared");
  start_advertising();
}
