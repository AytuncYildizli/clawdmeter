#pragma once
#include <cstdint>

namespace ble_hid {

// USB HID keyboard usage codes (Keyboard/Keypad Page 0x07)
enum class Key : uint8_t {
    None    = 0x00,
    Enter   = 0x28,
    Escape  = 0x29,
    Tab     = 0x2B,
    Space   = 0x2C,
};

// Modifier byte bitmask (left-hand side modifiers)
enum class Modifier : uint8_t {
    None   = 0x00,
    LCtrl  = 0x01,
    LShift = 0x02,
};

// Initialise the HID device. Reuses the NimBLE server created by ble_peer::begin().
// Must be called AFTER ble_peer::begin().
void begin();

// Send a single key press + release notification.
void send_key(Modifier mod, Key key);

}  // namespace ble_hid
