#pragma once
#include <lvgl.h>

namespace splash {

// Create the splash overlay covering 320x240 of `parent`, hidden by default.
// Idempotent: a second call is a no-op.
void init(lv_obj_t* parent);

// Show the overlay. When `codex_palette` is true, render a procedural teal
// "CODEX" placeholder; otherwise show the Clawd logo with the Claude accent
// border. Brings the overlay to the foreground.
void show(bool codex_palette);

// Hide the overlay.
void hide();

bool is_visible();

}  // namespace splash
