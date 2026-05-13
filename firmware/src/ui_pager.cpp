#include "ui_pager.h"

namespace ui_pager {

namespace {

void show_only(Pager& p, Page page) {
    for (int i = 0; i < 2; ++i) {
        if (static_cast<int>(page) == i) {
            lv_obj_clear_flag(p.screens[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(p.screens[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    p.current = page;
}

}  // namespace

void init(Pager& p, lv_obj_t* claude_screen, lv_obj_t* codex_screen) {
    p.screens[0] = claude_screen;
    p.screens[1] = codex_screen;
    show_only(p, Page::Claude);
}

void on_swipe_left(Pager& p) {
    show_only(p, p.current == Page::Claude ? Page::Codex : Page::Claude);
}

void on_swipe_right(Pager& p) {
    show_only(p, p.current == Page::Codex ? Page::Claude : Page::Codex);
}

Page current(const Pager& p) { return p.current; }

data::AgentKind current_agent(const Pager& p) {
    return p.current == Page::Claude ? data::AgentKind::Claude : data::AgentKind::Codex;
}

}  // namespace ui_pager
