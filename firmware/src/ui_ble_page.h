#pragma once
#include <lvgl.h>

namespace ui_ble_page {

// Build the third pager page — a BLE status screen — into `parent`.
// Returns the LVGL screen object so the pager can swap it in/out.
//
// Page layout (card-on-dark, matching the meter screens):
//   "BLE"                          (montserrat_20, centered top)
//   [dot] Connected / Disconnected (montserrat_16, state-colored)
//   Device: Clawd Controller       (montserrat_14, secondary)
//   ... credits at bottom ...
lv_obj_t* build(lv_obj_t* parent);

// Update the dot color + label text from the current BLE connection state.
// Called once per loop() iteration in main.cpp.
void refresh(bool connected);

}  // namespace ui_ble_page
