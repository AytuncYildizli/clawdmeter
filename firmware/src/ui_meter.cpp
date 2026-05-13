#include "ui_meter.h"
#include "theme.h"
#include "layout_math.h"
#include <cstdio>
#include <cstring>

namespace ui_meter {

namespace {

struct ScreenWidgets {
    lv_obj_t* root;
    lv_obj_t* provider_tag;
    lv_obj_t* big_number;
    lv_obj_t* reset_label;
    lv_obj_t* secondary_tick;
    lv_obj_t* repo_label;
    lv_obj_t* dot_left;
    lv_obj_t* dot_right;
};

ScreenWidgets g_claude;
ScreenWidgets g_codex;

ScreenWidgets build_screen(lv_obj_t* parent, lv_color_t accent) {
    ScreenWidgets w{};
    w.root = lv_obj_create(parent);
    lv_obj_set_size(w.root, layout::SCREEN_W, layout::SCREEN_H);
    lv_obj_set_style_bg_color(w.root, lv_color_hex(theme::BG), 0);
    lv_obj_set_style_border_width(w.root, 0, 0);
    lv_obj_set_style_pad_all(w.root, 0, 0);

    w.provider_tag = lv_label_create(w.root);
    lv_obj_set_style_text_color(w.provider_tag, accent, 0);
    lv_obj_set_style_text_font(w.provider_tag, &lv_font_montserrat_20, 0);
    lv_obj_align(w.provider_tag, LV_ALIGN_TOP_MID, 0, layout::TOP_MARGIN);

    w.big_number = lv_label_create(w.root);
    lv_obj_set_style_text_color(w.big_number, lv_color_hex(theme::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(w.big_number, &lv_font_montserrat_48, 0);
    lv_obj_align(w.big_number, LV_ALIGN_CENTER, 0, 0);

    w.reset_label = lv_label_create(w.root);
    lv_obj_set_style_text_color(w.reset_label, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(w.reset_label, &lv_font_montserrat_16, 0);
    lv_obj_align(w.reset_label, LV_ALIGN_CENTER, 0, 50);

    w.secondary_tick = lv_label_create(w.root);
    lv_obj_set_style_text_color(w.secondary_tick, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(w.secondary_tick, &lv_font_montserrat_16, 0);
    lv_obj_align(w.secondary_tick, LV_ALIGN_CENTER, 0, 70);

    w.repo_label = lv_label_create(w.root);
    lv_obj_set_style_text_color(w.repo_label, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(w.repo_label, &lv_font_montserrat_16, 0);
    lv_obj_align(w.repo_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    // Two indicator dots — small bars
    w.dot_left = lv_obj_create(w.root);
    lv_obj_set_size(w.dot_left, 8, 8);
    lv_obj_set_style_radius(w.dot_left, 4, 0);
    lv_obj_align(w.dot_left, LV_ALIGN_BOTTOM_MID, -10, -8);

    w.dot_right = lv_obj_create(w.root);
    lv_obj_set_size(w.dot_right, 8, 8);
    lv_obj_set_style_radius(w.dot_right, 4, 0);
    lv_obj_align(w.dot_right, LV_ALIGN_BOTTOM_MID, 10, -8);

    return w;
}

void refresh_one(ScreenWidgets& w, const data::ProviderBlock& block,
                 const data::FocusBlock& focus, const char* provider_name,
                 lv_color_t accent, bool is_active_pager, bool show_7d) {
    char buf[64];

    // Provider tag: "CLAUDE | 5H" or "CODEX | 7D" (ASCII pipe; Montserrat
    // subset lacks U+00B7 middle dot and renders it as a missing-glyph box).
    std::snprintf(buf, sizeof(buf), "%s | %s", provider_name, show_7d ? "7D" : "5H");
    lv_label_set_text(w.provider_tag, buf);

    // Big number
    if (!block.ok) {
        lv_label_set_text(w.big_number, "—");
    } else {
        int v = show_7d ? block.w : block.s;
        std::snprintf(buf, sizeof(buf), "%d%%", v);
        lv_label_set_text(w.big_number, buf);
    }

    // Reset countdown for the active frame
    if (block.ok) {
        int mins = show_7d ? block.wr : block.sr;
        std::snprintf(buf, sizeof(buf), "resets in %dh %dm", mins / 60, mins % 60);
        lv_label_set_text(w.reset_label, buf);
    } else {
        lv_label_set_text(w.reset_label, "");
    }

    // Secondary tick: the OTHER frame at a glance
    if (block.ok) {
        int v2 = show_7d ? block.s : block.w;
        const char* lbl = show_7d ? "5h" : "weekly";
        std::snprintf(buf, sizeof(buf), "%s %d%%", lbl, v2);
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
        std::strcpy(buf, "—");
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
