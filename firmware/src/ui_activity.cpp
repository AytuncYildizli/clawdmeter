#include "ui_activity.h"
#include "theme.h"
#include "layout_math.h"
#include <cstdio>
#include <cstring>

namespace ui_activity {

namespace {

constexpr int VISIBLE_ROWS = 6;  // device card fits ~6 lines comfortably

lv_obj_t* g_screen = nullptr;
lv_obj_t* g_card = nullptr;
lv_obj_t* g_title = nullptr;
lv_obj_t* g_rows[VISIBLE_ROWS] = {};

// Map daemon's one-char verb to a 2-char visual prefix (kept ASCII because
// Montserrat-14 subset doesn't include extended glyphs).
const char* verb_glyph(char v) {
    switch (v) {
        case '+': return "+ ";
        case '-': return "- ";
        case '>': return "> ";
        case '=': return "= ";
        default:  return "  ";
    }
}

// Map agent code to display letter. "c" -> "C" (Claude), "x" -> "X" (Codex).
char agent_char(char a) {
    switch (a) {
        case 'c': return 'C';
        case 'x': return 'X';
        default:  return '?';
    }
}

// Format epoch seconds as HH:MM (24h, local time). The device has no RTC
// sync to wall clock — but the daemon ships seconds-since-epoch and ESP32's
// time-since-boot starts at 0. We display the raw ts modulo 86400 in HH:MM
// form, which gives the correct time of day for any event the daemon emits
// after its first poll (the daemon DOES have wall clock).
void format_hhmm(uint32_t epoch, char* out, size_t cap) {
    uint32_t day_sec = epoch % 86400;
    uint32_t hh = day_sec / 3600;
    uint32_t mm = (day_sec % 3600) / 60;
    std::snprintf(out, cap, "%02u:%02u", (unsigned)hh, (unsigned)mm);
}

}  // namespace

lv_obj_t* build(lv_obj_t* parent) {
    g_screen = lv_obj_create(parent);
    lv_obj_set_size(g_screen, layout::SCREEN_W, layout::SCREEN_H);
    lv_obj_set_style_bg_color(g_screen, lv_color_hex(theme::BG), 0);
    lv_obj_set_style_border_width(g_screen, 0, 0);
    lv_obj_set_style_pad_all(g_screen, 0, 0);
    lv_obj_clear_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);

    g_card = lv_obj_create(g_screen);
    lv_obj_set_size(g_card, 300, 220);
    lv_obj_set_style_radius(g_card, 16, 0);
    lv_obj_set_style_bg_color(g_card, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_bg_opa(g_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_card, 1, 0);
    lv_obj_set_style_border_color(g_card, lv_color_hex(0x222222), 0);
    lv_obj_set_style_pad_all(g_card, 10, 0);
    lv_obj_clear_flag(g_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(g_card);

    g_title = lv_label_create(g_card);
    lv_obj_set_style_text_color(g_title, lv_color_hex(theme::CLAUDE_ACCENT), 0);
    lv_obj_set_style_text_font(g_title, &lv_font_montserrat_16, 0);
    lv_label_set_text(g_title, "ACTIVITY");
    lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, 0);

    // Six monospace-ish rows under the title. y stride = 28px keeps the
    // 6 rows + title within 220-20 padding.
    for (int i = 0; i < VISIBLE_ROWS; ++i) {
        g_rows[i] = lv_label_create(g_card);
        lv_obj_set_style_text_color(g_rows[i],
                                    lv_color_hex(theme::TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(g_rows[i], &lv_font_montserrat_14, 0);
        lv_obj_align(g_rows[i], LV_ALIGN_TOP_LEFT, 0, 24 + i * 28);
        lv_label_set_text(g_rows[i], "");
    }

    return g_screen;
}

void refresh(const data::PayloadState& state) {
    if (!g_screen) return;

    const int n = state.activity_event_count < VISIBLE_ROWS
                  ? state.activity_event_count : VISIBLE_ROWS;
    char buf[40];

    for (int i = 0; i < n; ++i) {
        const auto& ev = state.activity_events[i];
        char hhmm[8];
        format_hhmm(ev.ts_epoch, hhmm, sizeof(hhmm));
        std::snprintf(buf, sizeof(buf), "%s %s%c %s",
                      hhmm, verb_glyph(ev.verb),
                      agent_char(ev.agent), ev.repo);
        lv_label_set_text(g_rows[i], buf);
        // Color the row by the verb — started/stopped get accent tinting.
        uint32_t color = theme::TEXT_SECONDARY;
        if (ev.verb == '>') color = 0x44AA44;        // green for started
        else if (ev.verb == '=') color = 0xFFCC00;   // yellow for stopped
        else if (ev.verb == '+') color = theme::TEXT_PRIMARY;
        else if (ev.verb == '-') color = 0x888888;
        lv_obj_set_style_text_color(g_rows[i], lv_color_hex(color), 0);
    }
    // Clear unused rows so we don't show stale data.
    for (int i = n; i < VISIBLE_ROWS; ++i) {
        lv_label_set_text(g_rows[i], "");
    }
    if (n == 0) {
        lv_label_set_text(g_rows[0], "no activity yet");
    }
}

}  // namespace ui_activity
