#include "ble_peer.h"
#include "payload_parser.h"
#include <Arduino.h>
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
            // Stamp arrival time on the parsed state so the UI freshness footer
            // (Plan #4 Task 14) can compute age = millis() - last_payload_millis.
            // Set here (not in the consumer lambda) so any future callback path
            // automatically inherits the timestamp.
            parsed.last_payload_millis = millis();
            if (g_callback) g_callback(parsed);
        }
    }
};

class ServerCallbacks : public NimBLEServerCallbacks {
    void onDisconnect(NimBLEServer* server,
                      NimBLEConnInfo& /*info*/,
                      int /*reason*/) override {
        // NimBLE halts advertising when a peer connects and does NOT auto-resume
        // on disconnect. Without this restart, the device becomes invisible to
        // the daemon's scanner after the first BLE session ends.
        NimBLEDevice::startAdvertising();
    }
};

void begin(PayloadCallback cb) {
    g_callback = cb;

    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setMTU(517);  // request large MTU for payload writes

    g_server = NimBLEDevice::createServer();
    static ServerCallbacks server_cb;
    g_server->setCallbacks(&server_cb);
    NimBLEService* service = g_server->createService(SERVICE_UUID);

    // Third arg = max value length. Default in NimBLE-Arduino is small;
    // our payload grew with multi-account + activity feed to ~900-1000B.
    // Bumping to 1024 unblocks the "Invalid Attribute Value Length" (0x0D)
    // error we hit. Negotiated MTU (517) already supports long writes via
    // the prepare-write / execute-write GATT pair, so this just raises the
    // characteristic's declared ceiling.
    auto rx = service->createCharacteristic(
        RX_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR,
        1024
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

bool is_connected() {
    // NimBLE 2.x: getServer() returns the singleton created in begin();
    // getConnectedCount() reports active centrals. No subscription state
    // required — a paired daemon counts as connected.
    NimBLEServer* srv = NimBLEDevice::getServer();
    return srv != nullptr && srv->getConnectedCount() > 0;
}

}  // namespace ble_peer
