#include "splash.h"

#include "assets/clawd_animation.h"
#include "assets/codex_sprite.h"
#include "theme.h"

namespace splash {

namespace {

lv_obj_t* g_overlay = nullptr;
lv_obj_t* g_clawd_img = nullptr;
lv_obj_t* g_codex_img = nullptr;     // teal mascot (Gemini-generated 160x160)
bool g_visible = false;
lv_img_dsc_t g_frame_dsc[assets::CLAWD_FRAME_COUNT]{};
lv_img_dsc_t g_codex_dsc{};
int g_current_frame = 0;
uint32_t g_last_advance = 0;

// Per-frame display duration (ms). Sum = 2700ms = ~2.7s loop.
// Cycle: idle -> blink -> wave -> sleep (then wraps).
constexpr uint32_t FRAME_HOLD_MS[assets::CLAWD_FRAME_COUNT] = {
    1200,  // idle (long base pose)
    150,   // blink (fast)
    600,   // wave (medium hold)
    750,   // sleep (long, restful)
};

void apply_frame(int idx) {
    if (g_clawd_img == nullptr) return;
    if (idx < 0 || idx >= assets::CLAWD_FRAME_COUNT) return;
    lv_image_set_src(g_clawd_img, &g_frame_dsc[idx]);
    g_current_frame = idx;
}

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

    // Populate one LVGL image descriptor per animation frame.
    for (int i = 0; i < assets::CLAWD_FRAME_COUNT; ++i) {
        g_frame_dsc[i].header.cf = LV_COLOR_FORMAT_RGB565;
        g_frame_dsc[i].header.w = assets::CLAWD_FRAME_W;
        g_frame_dsc[i].header.h = assets::CLAWD_FRAME_H;
        g_frame_dsc[i].data = assets::CLAWD_FRAMES[i];
        g_frame_dsc[i].data_size = assets::CLAWD_FRAME_SIZES[i];
    }

    g_clawd_img = lv_image_create(g_overlay);
    lv_image_set_src(g_clawd_img, &g_frame_dsc[0]);
    lv_obj_align(g_clawd_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(g_clawd_img, LV_OBJ_FLAG_HIDDEN);

    // Codex teal mascot — single static frame for now (the Clawd animation
    // pipeline can be extended to Codex when we want a matching loop).
    g_codex_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    g_codex_dsc.header.w = assets::CODEX_W;
    g_codex_dsc.header.h = assets::CODEX_H;
    g_codex_dsc.data = assets::CODEX_DATA;
    g_codex_dsc.data_size = sizeof(assets::CODEX_DATA);
    g_codex_img = lv_image_create(g_overlay);
    lv_image_set_src(g_codex_img, &g_codex_dsc);
    lv_obj_align(g_codex_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(g_codex_img, LV_OBJ_FLAG_HIDDEN);
}

void show(bool codex_palette) {
    if (g_overlay == nullptr) {
        return;
    }

    if (codex_palette) {
        // Codex: teal mascot on a soft teal backdrop, mirroring the Claude
        // splash's orange-on-Clawd composition for visual symmetry.
        lv_obj_set_style_bg_color(g_overlay,
                                  lv_color_hex(theme::CODEX_ACCENT), 0);
        lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
        lv_obj_add_flag(g_clawd_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_codex_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Claude: animated Clawd with a soft orange backdrop for visual punch.
        lv_obj_set_style_bg_color(g_overlay,
                                  lv_color_hex(theme::CLAUDE_ACCENT), 0);
        lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
        lv_obj_clear_flag(g_clawd_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_codex_img, LV_OBJ_FLAG_HIDDEN);
        // Reset to idle frame so the user sees a known starting pose.
        apply_frame(0);
        g_last_advance = 0;  // tick() will reseed on the next call
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

void tick(uint32_t now_millis) {
    if (!g_visible || g_clawd_img == nullptr) return;
    // Only animate when the Clawd image is the visible content (not codex).
    if (lv_obj_has_flag(g_clawd_img, LV_OBJ_FLAG_HIDDEN)) return;

    if (g_last_advance == 0) {
        g_last_advance = now_millis;
        return;
    }
    uint32_t hold = FRAME_HOLD_MS[g_current_frame];
    if (now_millis - g_last_advance >= hold) {
        int next = (g_current_frame + 1) % assets::CLAWD_FRAME_COUNT;
        apply_frame(next);
        g_last_advance = now_millis;
    }
}

}  // namespace splash
