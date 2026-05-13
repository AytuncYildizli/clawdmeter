#include "splash.h"

#include "assets/clawd_sprite.h"
#include "theme.h"

namespace splash {

namespace {

lv_obj_t* g_overlay = nullptr;
lv_obj_t* g_clawd_img = nullptr;
lv_obj_t* g_codex_label = nullptr;
bool g_visible = false;
lv_img_dsc_t g_clawd_dsc{};

}  // namespace

void init(lv_obj_t* parent) {
    if (g_overlay != nullptr) {
        return;  // idempotent
    }

    g_overlay = lv_obj_create(parent);
    lv_obj_set_size(g_overlay, 320, 240);
    lv_obj_set_pos(g_overlay, 0, 0);
    lv_obj_set_style_bg_color(g_overlay, lv_color_hex(theme::BG), 0);
    lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_overlay, 0, 0);
    lv_obj_set_style_pad_all(g_overlay, 0, 0);
    lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);

    // Clawd image descriptor (RGB565 LE).
    g_clawd_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    g_clawd_dsc.header.w = assets::CLAWD_W;
    g_clawd_dsc.header.h = assets::CLAWD_H;
    g_clawd_dsc.data = assets::CLAWD_DATA;
    g_clawd_dsc.data_size = sizeof(assets::CLAWD_DATA);

    g_clawd_img = lv_image_create(g_overlay);
    lv_image_set_src(g_clawd_img, &g_clawd_dsc);
    lv_obj_align(g_clawd_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(g_clawd_img, LV_OBJ_FLAG_HIDDEN);

    // Procedural Codex placeholder — bold teal "CODEX" label centered on
    // the dark overlay. v1; real sprite art deferred to Plan #4.
    g_codex_label = lv_label_create(g_overlay);
    lv_label_set_text(g_codex_label, "CODEX");
    lv_obj_set_style_text_color(g_codex_label,
                                lv_color_hex(theme::CODEX_ACCENT), 0);
#if LV_FONT_MONTSERRAT_48
    lv_obj_set_style_text_font(g_codex_label, &lv_font_montserrat_48, 0);
#endif
    lv_obj_align(g_codex_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(g_codex_label, LV_OBJ_FLAG_HIDDEN);
}

void show(bool codex_palette) {
    if (g_overlay == nullptr) {
        return;
    }

    if (codex_palette) {
        lv_obj_set_style_bg_color(g_overlay, lv_color_hex(theme::BG), 0);
        lv_obj_add_flag(g_clawd_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_codex_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Claude: Clawd logo with a soft orange backdrop for visual punch.
        lv_obj_set_style_bg_color(g_overlay,
                                  lv_color_hex(theme::CLAUDE_ACCENT), 0);
        lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
        lv_obj_clear_flag(g_clawd_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_codex_label, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_overlay);
    g_visible = true;
}

void hide() {
    if (g_overlay == nullptr) {
        return;
    }
    lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
    g_visible = false;
}

bool is_visible() { return g_visible; }

}  // namespace splash
