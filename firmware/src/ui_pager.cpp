#include "ui_pager.h"

namespace ui_pager {

namespace {

constexpr int32_t SCREEN_W = 320;
constexpr uint32_t SLIDE_MS = 250;

bool g_animating = false;
lv_obj_t* g_outgoing = nullptr;

// Hide the outgoing screen after the slide-out finishes. Called by LVGL when
// the slide animation completes; clears the in-flight flag so back-to-back
// swipes don't stomp on each other.
void on_slide_done(lv_anim_t* /*a*/) {
    if (g_outgoing != nullptr) {
        lv_obj_add_flag(g_outgoing, LV_OBJ_FLAG_HIDDEN);
        // Restore the now-hidden screen to its default x so the next swap
        // doesn't start from an unexpected offset.
        lv_obj_set_x(g_outgoing, 0);
        g_outgoing = nullptr;
    }
    g_animating = false;
}

void start_slide(lv_obj_t* obj, int32_t from_x, int32_t to_x,
                 lv_anim_completed_cb_t done_cb) {
    lv_obj_set_x(obj, from_x);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from_x, to_x);
    lv_anim_set_duration(&a, SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, [](void* var, int32_t v) {
        lv_obj_set_x(static_cast<lv_obj_t*>(var), v);
    });
    if (done_cb) lv_anim_set_completed_cb(&a, done_cb);
    lv_anim_start(&a);
}

// Snap immediately (no animation) — used by init() and as a fallback when a
// slide is already in flight.
void show_only_immediate(Pager& p, Page page) {
    for (int i = 0; i < kPageCount; ++i) {
        lv_obj_set_x(p.screens[i], 0);
        if (static_cast<int>(page) == i) {
            lv_obj_clear_flag(p.screens[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(p.screens[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    p.current = page;
}

// Direction: -1 = next page slides in from the right (user swiped left);
//            +1 = next page slides in from the left (user swiped right).
void slide_to(Pager& p, Page next, int direction) {
    if (next == p.current) return;
    if (g_animating) {
        // Mid-animation: skip to the final state instead of layering anims.
        show_only_immediate(p, next);
        return;
    }
    lv_obj_t* outgoing = p.screens[static_cast<int>(p.current)];
    lv_obj_t* incoming = p.screens[static_cast<int>(next)];

    g_animating = true;
    g_outgoing = outgoing;
    p.current = next;

    // Outgoing slides off; incoming slides in from the opposite edge.
    int32_t outgoing_to = (direction < 0) ? -SCREEN_W : SCREEN_W;
    int32_t incoming_from = (direction < 0) ? SCREEN_W : -SCREEN_W;
    start_slide(outgoing, 0, outgoing_to, on_slide_done);
    start_slide(incoming, incoming_from, 0, nullptr);
}

// 3-page ring: claude(0) -> codex(1) -> activity(2) -> claude(0).
Page next_page(Page current, int dir) {
    int n = static_cast<int>(current) + (dir < 0 ? 1 : -1);
    if (n < 0) n = kPageCount - 1;
    if (n >= kPageCount) n = 0;
    return static_cast<Page>(n);
}

}  // namespace

void init(Pager& p, lv_obj_t* claude_screen, lv_obj_t* codex_screen,
          lv_obj_t* activity_screen) {
    p.screens[0] = claude_screen;
    p.screens[1] = codex_screen;
    p.screens[2] = activity_screen;
    show_only_immediate(p, Page::Claude);
}

void on_swipe_left(Pager& p) {
    // Forward cycle (claude -> codex -> activity -> claude).
    slide_to(p, next_page(p.current, -1), -1);
}

void on_swipe_right(Pager& p) {
    // Reverse cycle.
    slide_to(p, next_page(p.current, +1), +1);
}

Page current(const Pager& p) { return p.current; }

data::AgentKind current_agent(const Pager& p) {
    switch (p.current) {
        case Page::Claude: return data::AgentKind::Claude;
        case Page::Codex:  return data::AgentKind::Codex;
        default:           return data::AgentKind::None;
    }
}

}  // namespace ui_pager
