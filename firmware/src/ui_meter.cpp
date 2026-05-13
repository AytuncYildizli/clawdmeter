#include "ui_meter.h"
#include "theme.h"
#include "layout_math.h"
#include <M5Unified.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ui_meter {

namespace {

struct ScreenWidgets {
    lv_obj_t* root;
    lv_obj_t* card;
    lv_obj_t* provider_tag;
    lv_obj_t* big_number;
    lv_obj_t* progress_bar;
    lv_obj_t* reset_label;
    lv_obj_t* secondary_tick;
    lv_obj_t* repo_label;
    lv_obj_t* dot_left;
    lv_obj_t* dot_right;
    lv_obj_t* battery_icon;
    lv_obj_t* battery_fill;
    lv_obj_t* battery_label;
    lv_color_t accent;
};

ScreenWidgets g_claude;
ScreenWidgets g_codex;

ScreenWidgets build_screen(lv_obj_t* parent, lv_color_t accent) {
    ScreenWidgets w{};
    w.accent = accent;
    w.root = lv_obj_create(parent);
    lv_obj_set_size(w.root, layout::SCREEN_W, layout::SCREEN_H);
    lv_obj_set_style_bg_color(w.root, lv_color_hex(theme::BG), 0);
    lv_obj_set_style_border_width(w.root, 0, 0);
    lv_obj_set_style_pad_all(w.root, 0, 0);
    lv_obj_clear_flag(w.root, LV_OBJ_FLAG_SCROLLABLE);

    // Rounded card container (centered) — slightly lighter than pure black BG.
    w.card = lv_obj_create(w.root);
    lv_obj_set_size(w.card, 280, 200);
    lv_obj_set_style_radius(w.card, 16, 0);
    lv_obj_set_style_bg_color(w.card, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_bg_opa(w.card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(w.card, 1, 0);
    lv_obj_set_style_border_color(w.card, lv_color_hex(0x222222), 0);
    lv_obj_set_style_pad_all(w.card, 12, 0);
    lv_obj_clear_flag(w.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(w.card);

    w.provider_tag = lv_label_create(w.card);
    lv_obj_set_style_text_color(w.provider_tag, accent, 0);
    lv_obj_set_style_text_font(w.provider_tag, &lv_font_montserrat_20, 0);
    lv_obj_align(w.provider_tag, LV_ALIGN_TOP_MID, 0, 0);

    w.big_number = lv_label_create(w.card);
    lv_obj_set_style_text_color(w.big_number, lv_color_hex(theme::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(w.big_number, &lv_font_montserrat_48, 0);
    lv_obj_align(w.big_number, LV_ALIGN_CENTER, 0, 0);

    // Progress bar under the big number.
    w.progress_bar = lv_bar_create(w.card);
    lv_obj_set_size(w.progress_bar, 200, 8);
    lv_obj_align(w.progress_bar, LV_ALIGN_CENTER, 0, 50);
    lv_bar_set_range(w.progress_bar, 0, 100);
    lv_obj_set_style_radius(w.progress_bar, 4, 0);
    lv_obj_set_style_radius(w.progress_bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(w.progress_bar, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_bg_opa(w.progress_bar, LV_OPA_30, 0);
    lv_obj_set_style_bg_color(w.progress_bar, accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(w.progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);

    w.reset_label = lv_label_create(w.card);
    lv_obj_set_style_text_color(w.reset_label, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(w.reset_label, &lv_font_montserrat_16, 0);
    lv_obj_align(w.reset_label, LV_ALIGN_CENTER, 0, 60);

    w.secondary_tick = lv_label_create(w.card);
    lv_obj_set_style_text_color(w.secondary_tick, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(w.secondary_tick, &lv_font_montserrat_16, 0);
    lv_obj_align(w.secondary_tick, LV_ALIGN_CENTER, 0, 80);

    w.repo_label = lv_label_create(w.card);
    lv_obj_set_style_text_color(w.repo_label, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(w.repo_label, &lv_font_montserrat_16, 0);
    lv_obj_align(w.repo_label, LV_ALIGN_BOTTOM_MID, 0, -4);

    // Two indicator dots — small bars (outside the card, on the root).
    w.dot_left = lv_obj_create(w.root);
    lv_obj_set_size(w.dot_left, 8, 8);
    lv_obj_set_style_radius(w.dot_left, 4, 0);
    lv_obj_set_style_border_width(w.dot_left, 0, 0);
    lv_obj_align(w.dot_left, LV_ALIGN_BOTTOM_MID, -10, -8);

    w.dot_right = lv_obj_create(w.root);
    lv_obj_set_size(w.dot_right, 8, 8);
    lv_obj_set_style_radius(w.dot_right, 4, 0);
    lv_obj_set_style_border_width(w.dot_right, 0, 0);
    lv_obj_align(w.dot_right, LV_ALIGN_BOTTOM_MID, 10, -8);

    // Battery indicator (top-right of the root, outside the card).
    // Layout: [ "85%"  label ] [ outline 24x12 with inner fill ]
    w.battery_icon = lv_obj_create(w.root);
    lv_obj_set_size(w.battery_icon, 24, 12);
    lv_obj_set_style_radius(w.battery_icon, 2, 0);
    lv_obj_set_style_bg_opa(w.battery_icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w.battery_icon, 2, 0);
    lv_obj_set_style_border_color(w.battery_icon, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_pad_all(w.battery_icon, 0, 0);
    lv_obj_clear_flag(w.battery_icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(w.battery_icon, LV_ALIGN_TOP_RIGHT, -6, 6);

    w.battery_fill = lv_obj_create(w.battery_icon);
    lv_obj_set_size(w.battery_fill, 2, 6);
    lv_obj_set_style_radius(w.battery_fill, 1, 0);
    lv_obj_set_style_border_width(w.battery_fill, 0, 0);
    lv_obj_set_style_bg_color(w.battery_fill, lv_color_hex(0x44AA44), 0);
    lv_obj_set_style_bg_opa(w.battery_fill, LV_OPA_COVER, 0);
    lv_obj_align(w.battery_fill, LV_ALIGN_LEFT_MID, 1, 0);

    w.battery_label = lv_label_create(w.root);
    lv_obj_set_style_text_color(w.battery_label, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(w.battery_label, &lv_font_montserrat_14, 0);
    lv_obj_align(w.battery_label, LV_ALIGN_TOP_RIGHT, -34, 6);
    lv_label_set_text(w.battery_label, "--%");

    return w;
}

void refresh_battery(ScreenWidgets& w) {
    int level = M5.Power.getBatteryLevel();
    if (level < 0) level = 0;
    if (level > 100) level = 100;

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d%%", level);
    lv_label_set_text(w.battery_label, buf);

    int fill_w = (level * 20) / 100;
    if (fill_w < 2) fill_w = 2;
    lv_obj_set_width(w.battery_fill, fill_w);

    uint32_t color;
    if (level < 20) {
        color = 0xFF4444;
    } else if (level < 50) {
        color = 0xFFCC00;
    } else {
        color = 0x44AA44;
    }
    lv_obj_set_style_bg_color(w.battery_fill, lv_color_hex(color), 0);
}

void refresh_one(ScreenWidgets& w, const data::ProviderBlock& block,
                 const data::FocusBlock& focus, const char* provider_name,
                 lv_color_t accent, bool is_active_pager, bool /*show_7d*/) {
    char buf[64];

    // Provider tag: always "CLAUDE | weekly". 5h sits at the bottom as a small
    // secondary tick. Per user request 2026-05-13: no 5h/7d auto-rotate in the
    // big number; weekly is the primary, 5h is glance info.
    std::snprintf(buf, sizeof(buf), "%s | weekly", provider_name);
    lv_label_set_text(w.provider_tag, buf);

    // Big number = weekly utilization
    if (!block.ok) {
        lv_label_set_text(w.big_number, "--");
    } else {
        std::snprintf(buf, sizeof(buf), "%d%%", block.w);
        lv_label_set_text(w.big_number, buf);
    }

    // Progress bar = weekly utilization
    if (block.ok) {
        lv_obj_clear_flag(w.progress_bar, LV_OBJ_FLAG_HIDDEN);
        int v = block.w;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        lv_bar_set_value(w.progress_bar, v, LV_ANIM_ON);
    } else {
        lv_obj_add_flag(w.progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(w.progress_bar, 0, LV_ANIM_OFF);
    }

    // Reset countdown = weekly reset
    if (block.ok) {
        int mins = block.wr;
        std::snprintf(buf, sizeof(buf), "resets in %dh %dm", mins / 60, mins % 60);
        lv_label_set_text(w.reset_label, buf);
    } else {
        lv_label_set_text(w.reset_label, "");
    }

    // Secondary tick = the 5h glance ("5h X%")
    if (block.ok) {
        std::snprintf(buf, sizeof(buf), "5h %d%%", block.s);
        lv_label_set_text(w.secondary_tick, buf);
    } else {
        lv_label_set_text(w.secondary_tick, "");
    }

    // Repo label
    if (focus.sessions > 0) {
        std::snprintf(buf, sizeof(buf), "%s +%d", focus.repo, focus.sessions);
    } else if (focus.repo[0] != '\0') {
        std::snprintf(buf, sizeof(buf), "%s", focus.repo);
    } else {
        std::strcpy(buf, "--");
    }
    lv_label_set_text(w.repo_label, buf);

    // Dots — left/right reflect Claude/Codex; the *active* one uses the accent
    lv_color_t active_dot = accent;
    lv_color_t inactive_dot = lv_color_hex(theme::DOT_INACTIVE);
    if (is_active_pager) {
        // This screen IS the active pager position — bind dot colors below
    }
    lv_obj_set_style_bg_color(w.dot_left, is_active_pager ? active_dot : inactive_dot, 0);
    lv_obj_set_style_bg_color(w.dot_right, is_active_pager ? inactive_dot : active_dot, 0);

    // Battery — read once per refresh and update both visuals.
    refresh_battery(w);
}

}  // namespace

MeterScreens build(lv_obj_t* parent) {
    g_claude = build_screen(parent, lv_color_hex(theme::CLAUDE_ACCENT));
    g_codex  = build_screen(parent, lv_color_hex(theme::CODEX_ACCENT));
    // Both screens initially built; pager (Task 8) decides visibility.
    return { g_claude.root, g_codex.root };
}

void refresh(MeterScreens& /*screens*/, const data::PayloadState& state, bool show_7d) {
    // For v1, both screens always render; the pager controls visibility.
    refresh_one(g_claude, state.claude, state.focus, "CLAUDE",
                lv_color_hex(theme::CLAUDE_ACCENT),
                /*is_active_pager*/ true,  // pager will adjust per swipe
                show_7d);
    refresh_one(g_codex, state.codex, state.focus, "CODEX",
                lv_color_hex(theme::CODEX_ACCENT),
                /*is_active_pager*/ false,
                show_7d);
}

}  // namespace ui_meter
