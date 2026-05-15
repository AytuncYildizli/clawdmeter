#include "ui_activity.h"
#include "theme.h"
#include "layout_math.h"
#include <cstdio>
#include <cstring>

namespace ui_activity {

namespace {

// Single-event variant: BLE's 512B characteristic ceiling caps activity_events
// at 1 per write (see daemon/activity.py MAX_EVENTS=1). Page renders the most
// recent transition as a hero card instead of a many-line feed.
lv_obj_t* g_screen = nullptr;
lv_obj_t* g_card = nullptr;
lv_obj_t* g_title = nullptr;
lv_obj_t* g_time_lbl = nullptr;   // HH:MM in big font
lv_obj_t* g_verb_lbl = nullptr;   // green/yellow/white verb glyph
lv_obj_t* g_what_lbl = nullptr;   // "C rotator" in body font
lv_obj_t* g_empty_lbl = nullptr;  // shown when activity_event_count==0

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
    lv_label_set_text(g_title, "LATEST ACTIVITY");
    lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, 0);

    // Hero layout: time on top in big font, verb in accent color centered,
    // "what" body text below in medium font.
    g_time_lbl = lv_label_create(g_card);
    lv_obj_set_style_text_font(g_time_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(g_time_lbl, lv_color_hex(theme::TEXT_PRIMARY), 0);
    lv_label_set_text(g_time_lbl, "--:--");
    lv_obj_align(g_time_lbl, LV_ALIGN_CENTER, 0, -20);

    g_verb_lbl = lv_label_create(g_card);
    lv_obj_set_style_text_font(g_verb_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_verb_lbl, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_label_set_text(g_verb_lbl, "");
    lv_obj_align(g_verb_lbl, LV_ALIGN_CENTER, 0, 30);

    g_what_lbl = lv_label_create(g_card);
    lv_obj_set_style_text_font(g_what_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_what_lbl, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_label_set_text(g_what_lbl, "");
    lv_obj_align(g_what_lbl, LV_ALIGN_CENTER, 0, 60);

    g_empty_lbl = lv_label_create(g_card);
    lv_obj_set_style_text_font(g_empty_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_empty_lbl, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_label_set_text(g_empty_lbl, "no activity yet");
    lv_obj_align(g_empty_lbl, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_flag(g_empty_lbl, LV_OBJ_FLAG_HIDDEN);

    return g_screen;
}

void refresh(const data::PayloadState& state) {
    if (!g_screen) return;

    if (state.activity_event_count == 0) {
        // Nothing to show — hide the hero, show the empty hint.
        lv_obj_add_flag(g_time_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_verb_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_what_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_empty_lbl, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_add_flag(g_empty_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_time_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_verb_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_what_lbl, LV_OBJ_FLAG_HIDDEN);

    const auto& ev = state.activity_events[0];  // newest

    char hhmm[8];
    format_hhmm(ev.ts_epoch, hhmm, sizeof(hhmm));
    lv_label_set_text(g_time_lbl, hhmm);

    // Verb mapping to a readable word — better hero copy than the 1-char.
    const char* verb_word = "";
    uint32_t verb_color = theme::TEXT_SECONDARY;
    switch (ev.verb) {
        case '+': verb_word = "STARTED";  verb_color = theme::TEXT_PRIMARY; break;
        case '-': verb_word = "CLOSED";   verb_color = 0x888888; break;
        case '>': verb_word = "RUNNING";  verb_color = 0x44AA44; break;
        case '=': verb_word = "PAUSED";   verb_color = 0xFFCC00; break;
        default:  verb_word = "?";        break;
    }
    lv_label_set_text(g_verb_lbl, verb_word);
    lv_obj_set_style_text_color(g_verb_lbl, lv_color_hex(verb_color), 0);

    char what[32];
    const char* agent_name = (ev.agent == 'c') ? "Claude"
                            : (ev.agent == 'x') ? "Codex" : "?";
    std::snprintf(what, sizeof(what), "%s | %s", agent_name, ev.repo);
    lv_label_set_text(g_what_lbl, what);
}

}  // namespace ui_activity
