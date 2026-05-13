#pragma once
#include <lvgl.h>
#include "data.h"

namespace ui_pager {

enum class Page : uint8_t { Claude = 0, Codex = 1, Bluetooth = 2 };

struct Pager {
    lv_obj_t* screens[3];
    Page current = Page::Claude;
};

void init(Pager& p, lv_obj_t* claude_screen, lv_obj_t* codex_screen,
          lv_obj_t* ble_screen);
void on_swipe_left(Pager& p);   // claude -> codex -> bluetooth -> claude
void on_swipe_right(Pager& p);  // claude -> bluetooth -> codex -> claude
Page current(const Pager& p);
data::AgentKind current_agent(const Pager& p);

}  // namespace ui_pager
