#include "ble_peer.h"
#include "payload_parser.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

namespace ble_peer {

static NimBLEServer* g_server = nullptr;
static NimBLECharacteristic* g_req_char = nullptr;
static PayloadCallback g_callback;

// Chunked-write reassembly. BLE GATT caps a single characteristic value at
// 512 bytes (BLE_ATT_ATTR_MAX_LEN); larger payloads ship as multiple writes
// with a 1-byte marker prefix:
//   0x00 = first chunk (reset reassembly buffer, then append rest)
//   0x01 = middle chunk (append rest)
//   0x02 = last chunk (append rest, parse, reset)
// Daemon writes chunks with response=True so CoreBluetooth guarantees in-
// order delivery. Single-write payloads use marker=0x02 directly. A dropped
// mid-chunk is recovered by the next 0x00 marker; no retransmission state.
// For backward-compat: if marker is none of {0,1,2} (e.g. JSON starts with
// '{' = 0x7B), treat the whole value as a self-contained legacy payload.
class RxCallbacks : public NimBLECharacteristicCallbacks {
public:
    static constexpr size_t MAX_REASSEMBLY = 2048;
    uint8_t buffer[MAX_REASSEMBLY];
    size_t buf_len = 0;

    void deliver(const char* json, size_t n) {
        data::PayloadState parsed{};
        if (parser::parse_payload(json, n, parsed)) {
            parsed.last_payload_millis = millis();
            if (g_callback) g_callback(parsed);
        }
    }

    void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& /*info*/) override {
        const auto& value = chr->getValue();
        const size_t n = value.length();
        if (n == 0) return;

        const uint8_t marker = static_cast<uint8_t>(value.c_str()[0]);
        const bool is_chunked = (marker <= 0x02);

        if (!is_chunked) {
            // Legacy self-contained JSON write (no marker byte).
            deliver(value.c_str(), n);
            return;
        }

        const uint8_t* data = reinterpret_cast<const uint8_t*>(value.c_str()) + 1;
        const size_t data_len = n - 1;

        if (marker == 0x00) buf_len = 0;  // first chunk -> reset

        if (buf_len + data_len > MAX_REASSEMBLY) {
            Serial.printf("[ble-rx] overflow drop (had=%u +%u > %u)\n",
                          (unsigned)buf_len, (unsigned)data_len,
                          (unsigned)MAX_REASSEMBLY);
            buf_len = 0;
            return;
        }
        memcpy(buffer + buf_len, data, data_len);
        buf_len += data_len;

        if (marker == 0x02) {
            // Last chunk -> parse full reassembled JSON, reset for next payload.
            deliver(reinterpret_cast<const char*>(buffer), buf_len);
            buf_len = 0;
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
