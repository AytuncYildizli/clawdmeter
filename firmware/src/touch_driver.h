#pragma once
#include <cstdint>

namespace touch {

// Direct FT6x36 driver for Core 2.
//
// Why not M5Unified's M5.Touch?
//   Our diagnostic register-probe captured 169 finger events over 22s while
//   M5.Touch.getCount() returned 0 in every heartbeat. Both readers were
//   draining the same chip's event queue on the same I2C bus (Wire1 0x38);
//   M5Unified's internal poller lost the race to ours. Owning the I2C reads
//   in one place removes the contention.
//
// Coordinate system:
//   The FT6x36 reports finger position in the panel's NATIVE portrait
//   orientation (240 wide x 320 tall). We rotate to match the display
//   (M5.Display.setRotation(1) = 320 wide x 240 tall landscape):
//       landscape_x = native_y
//       landscape_y = 240 - native_x
//   (90 deg clockwise rotation.)

// Poll the touch chip once. Call at most ~50Hz; the chip's internal report
// rate is ~70Hz. Returns true if a contact is currently active (finger down).
bool update();

// Last-known position in landscape coordinates (320 wide, 240 tall). Valid
// only when update() returned true on the most recent call.
int16_t x();
int16_t y();

// Did the most recent update() see a press-down edge (i.e., a finger touched
// after being absent)? Consume-style — true exactly once per touch start.
bool consume_press_event();

}  // namespace touch
