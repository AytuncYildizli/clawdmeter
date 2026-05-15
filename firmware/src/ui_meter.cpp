#include "ui_meter.h"
#include "theme.h"
#include "layout_math.h"
#include <M5Unified.h>
#include <Arduino.h>
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
    lv_obj_t* status_dot;
    lv_obj_t* status_label;
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

    // Vertical stack inside the 256x176 card-content area (after 12px padding).
    // All widgets anchored to TOP_MID + explicit y so they don't overlap:
    //   y=0   provider_tag    (~22px tall, montserrat_20)
    //   y=26  big_number      (~52px tall, montserrat_48)
    //   y=84  progress_bar    (8px)
    //   y=98  reset_label     (~18px, montserrat_16)
    //   y=120 secondary_tick  (~18px, montserrat_16)
    //   y=142 repo_label      (~18px, montserrat_16)
    //   y=162 status row      (6px dot + 14px label centered together)

    w.provider_tag = lv_label_create(w.card);
    lv_obj_set_style_text_color(w.provider_tag, accent, 0);
    lv_obj_set_style_text_font(w.provider_tag, &lv_font_montserrat_20, 0);
    lv_obj_align(w.provider_tag, LV_ALIGN_TOP_MID, 0, 0);

    w.big_number = lv_label_create(w.card);
    lv_obj_set_style_text_color(w.big_number, lv_color_hex(theme::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(w.big_number, &lv_font_montserrat_48, 0);
    lv_obj_align(w.big_number, LV_ALIGN_TOP_MID, 0, 26);

    w.progress_bar = lv_bar_create(w.card);
    lv_obj_set_size(w.progress_bar, 200, 8);
    lv_obj_align(w.progress_bar, LV_ALIGN_TOP_MID, 0, 84);
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
    lv_obj_align(w.reset_label, LV_ALIGN_TOP_MID, 0, 98);

    w.secondary_tick = lv_label_create(w.card);
    lv_obj_set_style_text_color(w.secondary_tick, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(w.secondary_tick, &lv_font_montserrat_16, 0);
    lv_obj_align(w.secondary_tick, LV_ALIGN_TOP_MID, 0, 120);

    w.repo_label = lv_label_create(w.card);
    lv_obj_set_style_text_color(w.repo_label, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(w.repo_label, &lv_font_montserrat_16, 0);
    lv_obj_align(w.repo_label, LV_ALIGN_TOP_MID, 0, 142);

    // Freshness footer (Plan #4 Task 14): 6×6 colored dot + label inside the
    // card. Dot and label sit on the same row at y=162; dot is to the left.
    // Communicates BLE payload age: Waiting/Synced/Stale/Offline.
    w.status_dot = lv_obj_create(w.card);
    lv_obj_set_size(w.status_dot, 6, 6);
    lv_obj_set_style_radius(w.status_dot, 3, 0);
    lv_obj_set_style_border_width(w.status_dot, 0, 0);
    lv_obj_set_style_bg_color(w.status_dot, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_bg_opa(w.status_dot, LV_OPA_COVER, 0);
    lv_obj_align(w.status_dot, LV_ALIGN_TOP_MID, -36, 166);

    w.status_label = lv_label_create(w.card);
    lv_obj_set_style_text_color(w.status_label, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(w.status_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(w.status_label, "Waiting");
    lv_obj_align(w.status_label, LV_ALIGN_TOP_MID, 6, 162);

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

void refresh_status_footer(ScreenWidgets& w, uint32_t last_payload_millis) {
    // Compute age = millis() - last_payload_millis; pick label + dot color.
    // Buckets per Plan #4 Task 14:
    //   never:     "Waiting..." (gray)
    //   < 5 min:   "Synced"     (green 0x44AA44)
    //   < 60 min:  "Stale"      (yellow 0xFFCC00)
    //   >= 60 min: "Offline"    (red 0xFF4444)
    const char* label;
    uint32_t color;
    if (last_payload_millis == 0) {
        label = "Waiting...";
        color = theme::TEXT_SECONDARY;
    } else {
        uint32_t age_ms = millis() - last_payload_millis;
        if (age_ms < 300000U) {
            label = "Synced";
            color = 0x44AA44;
        } else if (age_ms < 3600000U) {
            label = "Stale";
            color = 0xFFCC00;
        } else {
            label = "Offline";
            color = 0xFF4444;
        }
    }
    lv_obj_set_style_bg_color(w.status_dot, lv_color_hex(color), 0);
    lv_label_set_text(w.status_label, label);
}

// Trim the "acct-" prefix the daemon emits for unnamed accounts so the
// rendered label is just the 4-char hash suffix (visual: "9b7c" not
// "acct-9b7c"). User-set labels (e.g. "work") are kept verbatim.
const char* short_label(const char* name) {
    if (name && std::strncmp(name, "acct-", 5) == 0) return name + 5;
    return name ? name : "?";
}

// Build the "5h glance / pool / switch suggestion" line that lives at y=120.
// Three modes:
//   1. Single-account pool: "5h X% left"
//   2. Multi-account pool, no urgency: "9b7c 87%   c1f2 92%"
//      (trimmed labels, double-space separator for breathing room)
//   3. Active drained AND a backup has headroom: "SWITCH -> 9b7c 92%"
//      (rendered in accent color to draw the eye)
// Returns true if it set the switch-suggestion (caller uses the accent color).
bool compose_pool_line(char* out, size_t cap,
                       const data::PayloadState& state,
                       int hourly_left, int weekly_left) {
    // Collect non-active accounts that are ok.
    int best_other_left = -1;
    const data::AccountSummary* best_other = nullptr;
    for (int i = 0; i < state.claude_account_count; ++i) {
        const auto& acc = state.claude_accounts[i];
        if (acc.active || !acc.ok) continue;
        int left_w = 100 - acc.w;
        if (left_w < 0) left_w = 0;
        if (left_w > best_other_left) {
            best_other_left = left_w;
            best_other = &acc;
        }
    }

    // Mode 3: switch suggestion.
    if (state.claude_account_count > 1
        && weekly_left >= 0 && weekly_left < 20
        && best_other != nullptr && best_other_left > 70) {
        std::snprintf(out, cap, "SWITCH -> %s %d%%",
                      short_label(best_other->name), best_other_left);
        return true;
    }

    // Mode 2: roll-up of up to 2 non-active accounts.
    if (state.claude_account_count > 1) {
        int written = 0;
        int rendered = 0;
        for (int i = 0; i < state.claude_account_count && rendered < 2; ++i) {
            const auto& acc = state.claude_accounts[i];
            if (acc.active) continue;
            int left_w = acc.ok ? (100 - acc.w) : 0;
            if (left_w < 0) left_w = 0;
            const char* sep = (rendered == 0) ? "" : "   ";  // 3-space separator
            int n = std::snprintf(out + written, cap - written,
                                  "%s%s %d%%", sep,
                                  short_label(acc.name), left_w);
            if (n < 0 || (size_t)(written + n) >= cap) break;
            written += n;
            rendered++;
        }
        if (rendered > 0) return false;
        // Fall through if we somehow had a count but no renderable rows.
    }

    // Mode 1: classic single-account 5h glance.
    std::snprintf(out, cap, "5h %d%% left", hourly_left);
    return false;
}

void refresh_one(ScreenWidgets& w, const data::PayloadState& state,
                 const data::ProviderBlock& block,
                 const data::FocusBlock& focus, const char* provider_name,
                 lv_color_t accent, bool is_active_pager, bool /*show_7d*/,
                 uint32_t last_payload_millis) {
    char buf[64];

    // Provider tag: always "CLAUDE | weekly". 5h sits at the bottom as a small
    // secondary tick. Per user request 2026-05-13: no 5h/7d auto-rotate in the
    // big number; weekly is the primary, 5h is glance info.
    std::snprintf(buf, sizeof(buf), "%s | weekly", provider_name);
    lv_label_set_text(w.provider_tag, buf);

    // Convert USED% (what daemon sends) to LEFT% (what we display). Matches
    // Codex `/status` and feels like a fuel/battery gauge: 100% left = fresh,
    // 0% left = exhausted.
    int weekly_left = block.ok ? (100 - block.w) : 0;
    int hourly_left = block.ok ? (100 - block.s) : 0;
    if (weekly_left < 0) weekly_left = 0;
    if (weekly_left > 100) weekly_left = 100;
    if (hourly_left < 0) hourly_left = 0;
    if (hourly_left > 100) hourly_left = 100;

    // Big number = weekly LEFT, color-coded by threshold:
    //   >= 30%  -> white (plenty)
    //   10-29%  -> yellow (getting low)
    //   <  10%  -> red (critical)
    // Same colors apply to the progress bar's indicator so both signal
    // urgency together. !ok keeps the dim gray placeholder.
    if (!block.ok) {
        lv_label_set_text(w.big_number, "--");
        lv_obj_set_style_text_color(w.big_number,
                                    lv_color_hex(theme::TEXT_PRIMARY), 0);
    } else {
        std::snprintf(buf, sizeof(buf), "%d%%", weekly_left);
        lv_label_set_text(w.big_number, buf);
        uint32_t big_color = theme::TEXT_PRIMARY;
        if (weekly_left < 10)      big_color = 0xFF4444;  // red
        else if (weekly_left < 30) big_color = 0xFFCC00;  // yellow
        lv_obj_set_style_text_color(w.big_number, lv_color_hex(big_color), 0);
        lv_obj_set_style_bg_color(w.progress_bar,
                                  lv_color_hex(big_color), LV_PART_INDICATOR);
    }

    // Progress bar = weekly LEFT (fills toward 100 when fresh, drains as used)
    if (block.ok) {
        lv_obj_clear_flag(w.progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(w.progress_bar, weekly_left, LV_ANIM_ON);
    } else {
        lv_obj_add_flag(w.progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(w.progress_bar, 0, LV_ANIM_OFF);
    }

    // Reset countdown = weekly reset, ticking down locally between probes.
    // The daemon ships `wr` = minutes-until-reset at probe time. We subtract
    // the elapsed time since the payload arrived so the display advances by
    // the minute even without a fresh probe (feels real-time).
    //   effective_mins = wr - (millis() - last_payload_millis) / 60000
    // When effective_mins <= 0, the next probe will refresh wr; clamp to 0
    // until then so we never display negatives.
    if (block.ok) {
        int mins = block.wr;
        if (last_payload_millis != 0) {
            uint32_t elapsed_ms = millis() - last_payload_millis;
            int elapsed_mins = static_cast<int>(elapsed_ms / 60000U);
            mins -= elapsed_mins;
            if (mins < 0) mins = 0;
        }
        if (mins >= 24 * 60) {
            int days = mins / (24 * 60);
            int hours = (mins % (24 * 60)) / 60;
            std::snprintf(buf, sizeof(buf), "resets in %dd %dh", days, hours);
        } else {
            std::snprintf(buf, sizeof(buf), "resets in %dh %dm", mins / 60, mins % 60);
        }
        lv_label_set_text(w.reset_label, buf);
    } else {
        lv_label_set_text(w.reset_label, "");
    }

    // Secondary tick — 3-mode: solo 5h glance, multi-account roll-up, or
    // switch-suggestion. Switch suggestion uses the accent color to grab
    // attention; the other two stay dim gray.
    if (block.ok) {
        bool switch_mode = compose_pool_line(buf, sizeof(buf), state,
                                             hourly_left, weekly_left);
        lv_label_set_text(w.secondary_tick, buf);
        lv_obj_set_style_text_color(w.secondary_tick,
                                    switch_mode ? accent : lv_color_hex(0x666666), 0);
    } else {
        lv_label_set_text(w.secondary_tick, "");
        lv_obj_set_style_text_color(w.secondary_tick, lv_color_hex(0x666666), 0);
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

    // Freshness footer (Synced/Stale/Offline/Waiting based on BLE payload age).
    refresh_status_footer(w, last_payload_millis);
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
    refresh_one(g_claude, state, state.claude, state.focus, "CLAUDE",
                lv_color_hex(theme::CLAUDE_ACCENT),
                /*is_active_pager*/ true,
                show_7d, state.last_payload_millis);
    refresh_one(g_codex, state, state.codex, state.focus, "CODEX",
                lv_color_hex(theme::CODEX_ACCENT),
                /*is_active_pager*/ false,
                show_7d, state.last_payload_millis);
}

}  // namespace ui_meter
