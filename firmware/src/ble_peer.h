#pragma once
#include <functional>
#include "data.h"

namespace ble_peer {

// Spec § 8.1 — public contract with daemon. Do not change.
constexpr const char* SERVICE_UUID  = "4c41555a-4465-7669-6365-000000000001";
constexpr const char* RX_CHAR_UUID  = "4c41555a-4465-7669-6365-000000000002";
constexpr const char* REQ_CHAR_UUID = "4c41555a-4465-7669-6365-000000000004";
constexpr const char* DEVICE_NAME   = "Clawd Controller";

using PayloadCallback = std::function<void(const data::PayloadState&)>;

void begin(PayloadCallback cb);
// Notify the daemon that the device wants a fresh payload (e.g., on boot)
void request_refresh();
// Call periodically from loop() — handles any pending work
void tick();
// True iff at least one BLE central is currently connected (NimBLE 2.x).
// Used by the BLE status page to render Connected vs Disconnected.
bool is_connected();

}  // namespace ble_peer
