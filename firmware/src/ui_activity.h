#pragma once
#include <lvgl.h>
#include "data.h"

namespace ui_activity {

// Build the activity feed screen as a child of `parent`. Returns the screen
// root, which the pager uses for show/hide. Same card aesthetic as the meter
// screens (rounded, dark, accent-tinted border).
lv_obj_t* build(lv_obj_t* parent);

// Render the feed from the payload's activity_events ring buffer. Called
// from the main refresh path on every tick. Cheap when nothing changed
// (LVGL diffs label text internally).
void refresh(const data::PayloadState& state);

}  // namespace ui_activity
