#pragma once
#include "data.h"

namespace buttons {

// Poll M5.BtnA/B/C state. Translates presses into BLE HID key events,
// scoped to the currently-focused agent. Must be called once per loop()
// AFTER M5.update().
void tick(data::AgentKind focus);

// If BtnB was pressed since last call, returns true once and clears the flag.
// Caller (main UI) uses this to toggle the splash screen.
bool consume_splash_toggle();

}  // namespace buttons
