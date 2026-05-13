#include "ui_ble_page.h"
#include "theme.h"
#include "layout_math.h"

namespace ui_ble_page {

namespace {

lv_obj_t* g_root = nullptr;
lv_obj_t* g_status_dot = nullptr;
lv_obj_t* g_status_label = nullptr;
bool g_last_connected = false;
bool g_dirty = true;  // force first refresh to write state regardless of cache

}  // namespace

lv_obj_t* build(lv_obj_t* parent) {
    // Root — same near-black background as the meter root for visual continuity.
    g_root = lv_obj_create(parent);
    lv_obj_set_size(g_root, layout::SCREEN_W, layout::SCREEN_H);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(theme::BG), 0);
    lv_obj_set_style_border_width(g_root, 0, 0);
    lv_obj_set_style_pad_all(g_root, 0, 0);
    lv_obj_clear_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);

    // Card — matches the meter card (0x0A0A0A bg, 1px 0x222222 border, 16r).
    lv_obj_t* card = lv_obj_create(g_root);
    lv_obj_set_size(card, 280, 200);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x222222), 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(card);

    // Title — "BLE" white, montserrat_20, top-center.
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, "BLE");
    lv_obj_set_style_text_color(title, lv_color_hex(theme::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    // Status row — colored dot + state label, vertically centered in the card.
    g_status_dot = lv_obj_create(card);
    lv_obj_set_size(g_status_dot, 10, 10);
    lv_obj_set_style_radius(g_status_dot, 5, 0);
    lv_obj_set_style_border_width(g_status_dot, 0, 0);
    lv_obj_set_style_bg_color(g_status_dot, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_bg_opa(g_status_dot, LV_OPA_COVER, 0);
    lv_obj_align(g_status_dot, LV_ALIGN_CENTER, -60, -10);

    g_status_label = lv_label_create(card);
    lv_label_set_text(g_status_label, "Disconnected");
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(g_status_label, LV_ALIGN_CENTER, 10, -10);

    // Device line — "Device: Clawd Controller" in secondary.
    lv_obj_t* device = lv_label_create(card);
    lv_label_set_text(device, "Device: Clawd Controller");
    lv_obj_set_style_text_color(device, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(device, &lv_font_montserrat_14, 0);
    lv_obj_align(device, LV_ALIGN_CENTER, 0, 20);

    // Credits — low-opacity secondary text at the bottom of the card.
    lv_obj_t* credit_a = lv_label_create(card);
    lv_label_set_text(credit_a, "Built by @hermannbjorgvin");
    lv_obj_set_style_text_color(credit_a, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(credit_a, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_opa(credit_a, LV_OPA_40, 0);
    lv_obj_align(credit_a, LV_ALIGN_BOTTOM_MID, 0, -18);

    lv_obj_t* credit_b = lv_label_create(card);
    lv_label_set_text(credit_b, "Port: @AytuncYildizli");
    lv_obj_set_style_text_color(credit_b, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(credit_b, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_opa(credit_b, LV_OPA_40, 0);
    lv_obj_align(credit_b, LV_ALIGN_BOTTOM_MID, 0, -2);

    return g_root;
}

void refresh(bool connected) {
    // Cache to avoid thrashing LVGL with redundant style writes — the pager
    // re-enters this page once per loop iteration even when nothing changed.
    if (!g_dirty && connected == g_last_connected) return;
    g_last_connected = connected;
    g_dirty = false;

    if (!g_status_dot || !g_status_label) return;

    if (connected) {
        // Reuse the Synced green (0x44AA44) for visual consistency with the
        // meter's freshness footer. TEXT_PRIMARY for the "Connected" label.
        lv_obj_set_style_bg_color(g_status_dot, lv_color_hex(0x44AA44), 0);
        lv_label_set_text(g_status_label, "Connected");
        lv_obj_set_style_text_color(g_status_label, lv_color_hex(theme::TEXT_PRIMARY), 0);
    } else {
        lv_obj_set_style_bg_color(g_status_dot, lv_color_hex(theme::TEXT_SECONDARY), 0);
        lv_label_set_text(g_status_label, "Disconnected");
        lv_obj_set_style_text_color(g_status_label, lv_color_hex(theme::TEXT_SECONDARY), 0);
    }
}

}  // namespace ui_ble_page
