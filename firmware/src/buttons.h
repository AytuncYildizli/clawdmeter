#pragma once
#include "data.h"

namespace buttons {

// Per-loop button update. Polls M5.BtnA/B/C and stages pending intents:
//   BtnA -> page-swipe-left
//   BtnB -> splash toggle
//   BtnC -> page-swipe-right
// Touch surface on this Core 2 variant is firmware-locked and reports zero
// contacts via I2C, so bezel buttons are the navigation primary. HID
// keystroke emit (when ble_hid is enabled) layered on top of BtnA/BtnC is
// preserved for future use but currently disabled.
void tick(data::AgentKind focused_agent);

// Returns true once per BtnB press (consume-style).
bool consume_splash_toggle();

// Returns -1 for swipe-left intent, +1 for swipe-right, 0 if no pending
// intent. Consume-style: a non-zero return clears the intent.
int consume_swipe_intent();

}  // namespace buttons
