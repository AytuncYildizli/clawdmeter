#pragma once
#include <lvgl.h>
#include "data.h"

namespace ui_pager {

enum class Page : uint8_t { Claude = 0, Codex = 1 };

struct Pager {
    lv_obj_t* screens[2];
    Page current = Page::Claude;
};

void init(Pager& p, lv_obj_t* claude_screen, lv_obj_t* codex_screen);
void on_swipe_left(Pager& p);   // claude -> codex
void on_swipe_right(Pager& p);  // codex -> claude
Page current(const Pager& p);
data::AgentKind current_agent(const Pager& p);

}  // namespace ui_pager
