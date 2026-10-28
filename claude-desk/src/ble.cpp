#include "ble.h"

#include <string.h>

#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace {
constexpr char kRxUuid[] = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char kTxUuid[] = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char kSvcUuid[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";

NimBLECharacteristic *tx_char = nullptr;
NimBLEServer *server = nullptr;
volatile bool connected = false;
QueueHandle_t line_queue = nullptr;
char accum[220];
size_t accum_len = 0;

void push_line(const char *line) {
  if (line_queue == nullptr) {
    return;
  }
  char copy[200];
  strncpy(copy, line, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = '\0';
  xQueueSend(line_queue, copy, 0);
}

void absorb(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const char c = static_cast<char>(data[i]);
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      accum[accum_len] = '\0';
      if (accum_len > 0) {
        push_line(accum);
      }
      accum_len = 0;
      continue;
    }
    if (accum_len + 1 < sizeof(accum)) {
      accum[accum_len++] = c;
    }
  }
}

class ServerCbs : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *svr, NimBLEConnInfo &conn) override {
    if (svr->getConnectedCount() > 1) {
      svr->disconnect(conn.getConnHandle());
      return;
    }
    connected = true;
    accum_len = 0;
  }

  void onDisconnect(NimBLEServer *svr, NimBLEConnInfo &conn, int reason) override {
    (void)conn;
    (void)reason;
    if (svr->getConnectedCount() == 0) {
      connected = false;
      accum_len = 0;
      NimBLEDevice::startAdvertising();
    }
  }
};

class RxCbs : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &conn) override {
    (void)conn;
    const NimBLEAttValue value = characteristic->getValue();
    absorb(value.data(), value.length());
  }
};

class TxCbs : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *characteristic, NimBLEConnInfo &conn, uint16_t subValue) override {
    (void)characteristic;
    (void)conn;
    if (subValue != 0) {
      const char hello[] = "H\n";
      tx_char->notify(reinterpret_cast<const uint8_t *>(hello), sizeof(hello) - 1);
    }
  }
};

ServerCbs server_cbs;
RxCbs rx_cbs;
TxCbs tx_cbs;
}  // namespace

void ble_init(void) {
  line_queue = xQueueCreate(12, 200);
  NimBLEDevice::init("EinkSessions");
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  server = NimBLEDevice::createServer();
  server->setCallbacks(&server_cbs);
  NimBLEService *svc = server->createService(kSvcUuid);
  NimBLECharacteristic *rx = svc->createCharacteristic(kRxUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(&rx_cbs);
  tx_char = svc->createCharacteristic(kTxUuid, NIMBLE_PROPERTY::NOTIFY);
  tx_char->setCallbacks(&tx_cbs);
  server->start();
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(kSvcUuid);
  adv->setName("EinkSessions");
  adv->enableScanResponse(true);
  adv->start();
}

bool ble_connected(void) {
  return connected;
}

void ble_send_line(const char *line) {
  if (!connected || tx_char == nullptr || line == nullptr) {
    return;
  }
  char buf[200];
  const size_t n = strnlen(line, sizeof(buf) - 2);
  memcpy(buf, line, n);
  buf[n] = '\n';
  tx_char->notify(reinterpret_cast<const uint8_t *>(buf), n + 1);
}

bool ble_take_line(char *dst, size_t len) {
  if (line_queue == nullptr || dst == nullptr || len == 0) {
    return false;
  }
  char copy[200];
  if (xQueueReceive(line_queue, copy, 0) != pdTRUE) {
    return false;
  }
  strncpy(dst, copy, len - 1);
  dst[len - 1] = '\0';
  return true;
}
