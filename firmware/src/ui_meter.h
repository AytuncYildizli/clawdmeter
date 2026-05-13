#pragma once
#include <lvgl.h>
#include "data.h"

namespace ui_meter {

// Builds two screens (Claude + Codex) into the given LVGL parent.
// Returns the LVGL screen objects so the pager can swap between them.
struct MeterScreens {
    lv_obj_t* claude_screen;
    lv_obj_t* codex_screen;
};

MeterScreens build(lv_obj_t* parent);

// Refresh contents from a parsed payload. Caller chooses which frame
// (5h or 7d) is currently active via the auto-rotate state machine.
void refresh(MeterScreens& screens, const data::PayloadState& state, bool show_7d);

}  // namespace ui_meter
