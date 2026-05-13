#include "ui_pager.h"

namespace ui_pager {

void init(Pager& p, lv_obj_t* claude_screen, lv_obj_t* codex_screen) {
    p.screens[0] = claude_screen;
    p.screens[1] = codex_screen;
    p.current = Page::Claude;
    lv_obj_clear_flag(claude_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(codex_screen, LV_OBJ_FLAG_HIDDEN);
}

void on_swipe_left(Pager& p) {
    if (p.current == Page::Claude) {
        p.current = Page::Codex;
        lv_obj_add_flag(p.screens[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(p.screens[1], LV_OBJ_FLAG_HIDDEN);
    }
}

void on_swipe_right(Pager& p) {
    if (p.current == Page::Codex) {
        p.current = Page::Claude;
        lv_obj_add_flag(p.screens[1], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(p.screens[0], LV_OBJ_FLAG_HIDDEN);
    }
}

Page current(const Pager& p) { return p.current; }

data::AgentKind current_agent(const Pager& p) {
    return (p.current == Page::Claude) ? data::AgentKind::Claude : data::AgentKind::Codex;
}

}  // namespace ui_pager
