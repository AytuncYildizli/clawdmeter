#pragma once
#include <lvgl.h>
#include <cstdint>

namespace splash {

// Create the splash overlay covering 320x240 of `parent`, hidden by default.
// Idempotent: a second call is a no-op.
void init(lv_obj_t* parent);

// Show the overlay. When `codex_palette` is true, render a procedural teal
// "CODEX" placeholder; otherwise show the animated Clawd sprite with the
// Claude accent backdrop. Brings the overlay to the foreground.
void show(bool codex_palette);

// Hide the overlay.
void hide();

bool is_visible();

// Advance the Clawd animation if the splash is currently visible. Call from
// the main loop. Internal cadence: ~5 FPS (200ms per frame). The cycle is:
//   idle (600ms) -> blink (100ms) -> idle (600ms) -> wave (400ms) ->
//   idle (600ms) -> sleep (400ms) -> repeat
// Total cycle ~2.7s, expressive without spamming the redraw path.
void tick(uint32_t now_millis);

}  // namespace splash
