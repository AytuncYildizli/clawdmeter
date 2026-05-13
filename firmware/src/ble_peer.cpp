#include "ble_peer.h"
#include "payload_parser.h"
#include <NimBLEDevice.h>

namespace ble_peer {

static NimBLEServer* g_server = nullptr;
static NimBLECharacteristic* g_req_char = nullptr;
static PayloadCallback g_callback;

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& /*info*/) override {
        const auto& value = chr->getValue();
        data::PayloadState parsed{};
        if (parser::parse_payload(value.c_str(), value.length(), parsed)) {
            if (g_callback) g_callback(parsed);
        }
    }
};

void begin(PayloadCallback cb) {
    g_callback = cb;

    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setMTU(517);  // request large MTU for payload writes

    g_server = NimBLEDevice::createServer();
    NimBLEService* service = g_server->createService(SERVICE_UUID);

    auto rx = service->createCharacteristic(
        RX_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    static RxCallbacks rx_cb;
    rx->setCallbacks(&rx_cb);

    g_req_char = service->createCharacteristic(
        REQ_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ
    );

    service->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setName(DEVICE_NAME);
    // Service UUID intentionally omitted from the main adv packet — the
    // 128-bit UUID (18 bytes) + flags (3) + name (18) = 39 bytes, exceeding
    // the 31-byte BLE advertising limit. The daemon's scanner filters by name
    // only; the service is discovered after connection. To re-introduce the
    // UUID, move it to scan response data via setScanResponseData().
    adv->start();
}

void request_refresh() {
    if (g_req_char) {
        const uint8_t payload[] = { 'r' };
        g_req_char->setValue(payload, sizeof(payload));
        g_req_char->notify();
    }
}

void tick() {
    // NimBLE handles its own event pump; nothing required here for now.
}

}  // namespace ble_peer
