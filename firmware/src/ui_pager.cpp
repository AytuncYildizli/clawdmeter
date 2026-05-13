#include "ui_pager.h"

namespace ui_pager {

namespace {

void show_only(Pager& p, Page page) {
    for (int i = 0; i < 3; ++i) {
        if (static_cast<int>(page) == i) {
            lv_obj_clear_flag(p.screens[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(p.screens[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    p.current = page;
}

}  // namespace

void init(Pager& p, lv_obj_t* claude_screen, lv_obj_t* codex_screen,
          lv_obj_t* ble_screen) {
    p.screens[0] = claude_screen;
    p.screens[1] = codex_screen;
    p.screens[2] = ble_screen;
    show_only(p, Page::Claude);
}

void on_swipe_left(Pager& p) {
    // Claude -> Codex -> Bluetooth -> Claude (wrap)
    switch (p.current) {
        case Page::Claude:    show_only(p, Page::Codex);     break;
        case Page::Codex:     show_only(p, Page::Bluetooth); break;
        case Page::Bluetooth: show_only(p, Page::Claude);    break;
    }
}

void on_swipe_right(Pager& p) {
    // Bluetooth -> Codex -> Claude -> Bluetooth (wrap)
    switch (p.current) {
        case Page::Bluetooth: show_only(p, Page::Codex);     break;
        case Page::Codex:     show_only(p, Page::Claude);    break;
        case Page::Claude:    show_only(p, Page::Bluetooth); break;
    }
}

Page current(const Pager& p) { return p.current; }

data::AgentKind current_agent(const Pager& p) {
    // BLE page surfaces no agent — caller treats this as "no preference".
    switch (p.current) {
        case Page::Claude: return data::AgentKind::Claude;
        case Page::Codex:  return data::AgentKind::Codex;
        case Page::Bluetooth:
        default:           return data::AgentKind::None;
    }
}

}  // namespace ui_pager
