#ifndef UNIT_TEST

#include <M5Unified.h>
#include <Wire.h>
#include <lvgl.h>
#include <cstring>
#include "data.h"
#include "ui_meter.h"
#include "ui_pager.h"
#include "auto_rotate.h"
#include "ble_peer.h"
#include "ble_hid.h"
#include "buttons.h"
#include "splash.h"
#include "touch_driver.h"
#include "theme.h"

// LVGL display buffer — partial mode, 10 scanlines × 2 bytes/pixel (RGB565).
// Important: lv_color_t in LVGL 9 is a 3-byte {r,g,b} struct (sizeof=3), NOT a
// uint16_t. Using lv_color_t here would mis-stride the framebuffer — LVGL writes
// 2-byte pixels but our reads would skip by 3. Allocate as raw bytes instead.
static lv_display_t* g_disp;
static uint8_t g_buf1[320 * 10 * 2];

static void disp_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    int32_t w = (area->x2 - area->x1 + 1);
    int32_t h = (area->y2 - area->y1 + 1);
    // Use M5GFX's high-level pushImage which handles addrWindow + byte-order
    // correctly for the ILI9342C panel. The cast to uint16_t* is safe because
    // LV_COLOR_DEPTH=16 packs lv_color_t as a 16-bit RGB565 value.
    M5.Display.pushImage(area->x1, area->y1, w, h,
                        reinterpret_cast<lgfx::rgb565_t*>(px_map));
    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    auto t = M5.Touch.getDetail();
    if (t.isPressed()) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = t.x;
        data->point.y = t.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static ui_meter::MeterScreens g_meter;
static ui_pager::Pager g_pager;
static rotate::State g_rotate;
static data::PayloadState g_state{};

// Idle-dimming + auto-rotate-hint state. last_activity_ms is touched by any
// interaction (swipe, button, touch). After IDLE_DIM_MS without activity the
// LCD backlight drops from FULL to DIM; any activity restores immediately.
static uint32_t g_last_activity_ms = 0;
static bool g_dimmed = false;
constexpr uint32_t IDLE_DIM_MS = 30000;     // 30s
constexpr uint8_t BACKLIGHT_FULL = 200;     // ~80% — comfortable indoors
constexpr uint8_t BACKLIGHT_DIM = 40;       // ~16% — visible but discreet

// Auto-rotate countdown widgets (live on lv_scr_act() so they persist across
// pager swaps). A 320x2 thin bar at y=238 drains from full width to 0 over
// PAGE_SWAP_INTERVAL, then resets when the pager swaps.
static lv_obj_t* g_rotate_hint = nullptr;
static uint32_t g_last_page_swap_ms = 0;
constexpr uint32_t PAGE_SWAP_INTERVAL_MS = 10000;

// Battery-detail overlay (held BtnB shows it for ~1.5s). Lives on lv_scr_act
// foreground; rendered as a translucent card with voltage + charging state.
static lv_obj_t* g_battery_overlay = nullptr;
static lv_obj_t* g_battery_overlay_lbl = nullptr;
static uint32_t g_battery_overlay_hide_at_ms = 0;

void mark_activity() {
    g_last_activity_ms = millis();
    if (g_dimmed) {
        M5.Display.setBrightness(BACKLIGHT_FULL);
        g_dimmed = false;
    }
}

void build_rotate_hint() {
    g_rotate_hint = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_rotate_hint, 320, 2);
    lv_obj_set_pos(g_rotate_hint, 0, 238);
    lv_obj_set_style_radius(g_rotate_hint, 0, 0);
    lv_obj_set_style_border_width(g_rotate_hint, 0, 0);
    lv_obj_set_style_bg_color(g_rotate_hint, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_bg_opa(g_rotate_hint, LV_OPA_50, 0);
    lv_obj_clear_flag(g_rotate_hint, LV_OBJ_FLAG_SCROLLABLE);
}

void build_battery_overlay() {
    g_battery_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_battery_overlay, 200, 80);
    lv_obj_center(g_battery_overlay);
    lv_obj_set_style_radius(g_battery_overlay, 12, 0);
    lv_obj_set_style_bg_color(g_battery_overlay, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(g_battery_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_width(g_battery_overlay, 1, 0);
    lv_obj_set_style_border_color(g_battery_overlay, lv_color_hex(theme::CLAUDE_ACCENT), 0);
    lv_obj_set_style_pad_all(g_battery_overlay, 8, 0);
    lv_obj_clear_flag(g_battery_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_battery_overlay, LV_OBJ_FLAG_HIDDEN);

    g_battery_overlay_lbl = lv_label_create(g_battery_overlay);
    lv_obj_set_style_text_color(g_battery_overlay_lbl, lv_color_hex(theme::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(g_battery_overlay_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(g_battery_overlay_lbl);
    lv_label_set_text(g_battery_overlay_lbl, "");
}

void show_battery_overlay() {
    if (!g_battery_overlay || !g_battery_overlay_lbl) return;
    int level = M5.Power.getBatteryLevel();
    int voltage_mv = M5.Power.getBatteryVoltage();  // mV
    bool charging = M5.Power.isCharging();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d%%   %.2fV\n%s",
                  level, voltage_mv / 1000.0,
                  charging ? "charging" : "on battery");
    lv_label_set_text(g_battery_overlay_lbl, buf);
    lv_obj_clear_flag(g_battery_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_battery_overlay);
    g_battery_overlay_hide_at_ms = millis() + 1500;
}

void hide_battery_overlay() {
    if (!g_battery_overlay) return;
    lv_obj_add_flag(g_battery_overlay, LV_OBJ_FLAG_HIDDEN);
}

// Hand-rolled swipe detector. LVGL 9's gesture engine needs many polled
// samples per second to detect motion; with our 50ms loop the sampling is
// too sparse. Track touch start/end ourselves and fire on_swipe_left/right
// when horizontal travel exceeds a threshold.
namespace {

constexpr int16_t SWIPE_THRESHOLD_PX = 40;
constexpr int16_t SWIPE_VERTICAL_LIMIT_PX = 60;  // reject mostly-vertical drags
int16_t g_touch_start_x = -1;
int16_t g_touch_start_y = -1;
int16_t g_touch_last_x = -1;
int16_t g_touch_last_y = -1;
bool g_touch_active = false;

// Poll the FT6x36 touch driver and detect horizontal swipes.
//
// Why a hand-rolled detector instead of LVGL gestures? Two reasons:
// 1. LVGL 9's gesture engine needs many polled samples per second to detect
//    motion; our 50ms loop gives ~20Hz which is below its threshold.
// 2. M5Unified's M5.Touch was racing our diagnostic raw-I2C reads on the
//    same bus and consistently returning count=0 — we proved this with
//    169 [reg] events vs 0 [count>0] heartbeats in the same window. We
//    bypass M5.Touch entirely and own the FT6x36 polling via touch_driver.
void poll_swipe() {
    bool pressed = touch::update();
    int16_t tx = touch::x();
    int16_t ty = touch::y();

    if (pressed) {
        if (!g_touch_active) {
            g_touch_active = true;
            g_touch_start_x = tx;
            g_touch_start_y = ty;
            mark_activity();  // any finger contact wakes the dimmed screen
        }
        g_touch_last_x = tx;
        g_touch_last_y = ty;
    } else if (g_touch_active) {
        g_touch_active = false;
        if (g_touch_start_x < 0 || g_touch_last_x < 0) return;
        int16_t dx = g_touch_last_x - g_touch_start_x;
        int16_t dy = g_touch_last_y - g_touch_start_y;
        int16_t adx = dx < 0 ? -dx : dx;
        int16_t ady = dy < 0 ? -dy : dy;
        if (adx >= SWIPE_THRESHOLD_PX && ady <= SWIPE_VERTICAL_LIMIT_PX) {
            if (dx < 0) {
                ui_pager::on_swipe_left(g_pager);
            } else {
                ui_pager::on_swipe_right(g_pager);
            }
            g_last_page_swap_ms = millis();  // reset auto-rotate timer too
        }
    }
}

}  // namespace

static void screen_gesture_cb(lv_event_t* /*e*/) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_LEFT) {
        ui_pager::on_swipe_left(g_pager);
    } else if (dir == LV_DIR_RIGHT) {
        ui_pager::on_swipe_right(g_pager);
    }
}

void setup() {
    auto cfg = M5.config();
    cfg.internal_imu = false;  // we don't use it; avoid contending on the I2C bus
    cfg.internal_rtc = false;
    M5.begin(cfg);
    Serial.begin(115200);
    delay(100);
    Serial.println("\n[boot] Clawdmeter starting...");
    M5.Display.setRotation(1);  // 320×240 landscape; Core 2's ILI9342C is native portrait

    // ─── TOUCH CHIP IDENTIFICATION ─────────────────────────────────────────
    // Stock Core 2 ships FT6336U at Wire1 0x38, but this device returns chipid
    // 0x11 — wrong for FT6336U (0x64) and ambiguous for FT6206 (also 0x11) vs
    // a totally different chip family. Scan BOTH I2C buses, identify every
    // responder, then probe known touch-chip identity registers per family.
    Serial.printf("[touch] M5.getBoard=%d (M5Stack=0/M5StackCore2=2/CoreS3=4)\n", (int)M5.getBoard());
    Serial.printf("[touch] M5.Touch isEnabled=%d\n", (int)M5.Touch.isEnabled());

    auto scan_bus = [](TwoWire& bus, const char* name) {
        Serial.printf("[i2c-scan] === bus %s ===\n", name);
        int found = 0;
        for (uint8_t addr = 0x03; addr < 0x78; ++addr) {
            bus.beginTransmission(addr);
            uint8_t err = bus.endTransmission();
            if (err == 0) {
                Serial.printf("[i2c-scan] %s ACK @ 0x%02X\n", name, addr);
                ++found;
            }
        }
        Serial.printf("[i2c-scan] %s done, %d device(s)\n", name, found);
    };
    auto read_reg8 = [](TwoWire& bus, uint8_t addr, uint8_t reg) -> int {
        bus.beginTransmission(addr);
        bus.write(reg);
        if (bus.endTransmission(false) != 0) return -1;
        bus.requestFrom(addr, (uint8_t)1);
        if (!bus.available()) return -1;
        return bus.read();
    };
    auto read_reg16 = [](TwoWire& bus, uint8_t addr, uint16_t reg, uint8_t* buf, size_t n) -> bool {
        // 16-bit register address (big-endian) — used by GT911 and similar
        bus.beginTransmission(addr);
        bus.write((reg >> 8) & 0xFF);
        bus.write(reg & 0xFF);
        if (bus.endTransmission(false) != 0) return false;
        bus.requestFrom(addr, (uint8_t)n);
        for (size_t i = 0; i < n; ++i) {
            if (!bus.available()) return false;
            buf[i] = bus.read();
        }
        return true;
    };

    scan_bus(Wire, "Wire(bus0)");
    scan_bus(Wire1, "Wire1(bus1)");

    // Identify chip families. Try each candidate address on Wire1 (Core 2 panel).
    // FT6x06/FT6x36: 0x38, chipid @ 0xA8 (0x06=FT6206 0x36=FT6236 0x11=??? 0x64=FT6336U)
    // GT911:          0x5D or 0x14, product-id at 16-bit reg 0x8140..0x8143 (ASCII "911")
    // CST816S:        0x15, chipid at 0xA7 (returns 0xB4 for CST816S, 0xB5 CST716)
    // CHSC6540:       0x2E, varies
    // TT21100:        0x24, model id at 16-bit reg 0x0007
    {
        int v = read_reg8(Wire1, 0x38, 0xA8);
        Serial.printf("[touch-id] Wire1 0x38 reg 0xA8 = 0x%02X (FT6336U=0x64, FT6236=0x36, FT6206=0x11)\n", v);
        int firm = read_reg8(Wire1, 0x38, 0xA6);
        int vendor = read_reg8(Wire1, 0x38, 0xA3);
        Serial.printf("[touch-id] Wire1 0x38 firm=0x%02X vendor=0x%02X\n", firm, vendor);
    }
    for (uint8_t addr : {0x5D, 0x14}) {
        uint8_t buf[4] = {0};
        if (read_reg16(Wire1, addr, 0x8140, buf, 4)) {
            Serial.printf("[touch-id] Wire1 0x%02X GT911 product-id = '%c%c%c%c' (expect '911\\0')\n",
                          addr, buf[0], buf[1], buf[2], buf[3]);
        } else {
            Serial.printf("[touch-id] Wire1 0x%02X GT911 read FAILED\n", addr);
        }
    }
    {
        int chip = read_reg8(Wire1, 0x15, 0xA7);
        Serial.printf("[touch-id] Wire1 0x15 reg 0xA7 = 0x%02X (CST816S=0xB4, CST716=0xB5)\n", chip);
    }
    // Same probe panel on Wire (bus 0) — some Core 2 variants wire touch to bus 0.
    {
        int v = read_reg8(Wire, 0x38, 0xA8);
        Serial.printf("[touch-id] Wire 0x38 reg 0xA8 = 0x%02X\n", v);
    }
    Serial.println("[touch] === scan done ===");
    // ───────────────────────────────────────────────────────────────────────

    // Clean boot — black screen + dim wordmark briefly. The bright-orange
    // debug splash from the flash-iteration era is gone now that we trust
    // the pipeline; this gives a subtle "device is alive" beat before the
    // meter appears.
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(0x4208, TFT_BLACK);  // soft dim gray
    M5.Display.setTextSize(1);
    M5.Display.setCursor(100, 115);
    M5.Display.print("clawdmeter");
    delay(500);

    lv_init();
    g_disp = lv_display_create(320, 240);
    lv_display_set_flush_cb(g_disp, disp_flush_cb);
    lv_display_set_buffers(g_disp, g_buf1, nullptr, sizeof(g_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    g_meter = ui_meter::build(lv_scr_act());
    ui_pager::init(g_pager, g_meter.claude_screen, g_meter.codex_screen);
    // Register gesture handler on each pager screen — events don't bubble from
    // child widgets to lv_scr_act() automatically; the screen widgets themselves
    // own the touch area, so we attach there.
    lv_obj_add_event_cb(g_meter.claude_screen, screen_gesture_cb, LV_EVENT_GESTURE, nullptr);
    lv_obj_add_event_cb(g_meter.codex_screen, screen_gesture_cb, LV_EVENT_GESTURE, nullptr);
    lv_obj_add_event_cb(lv_scr_act(), screen_gesture_cb, LV_EVENT_GESTURE, nullptr);
    splash::init(lv_scr_act());
    build_rotate_hint();
    build_battery_overlay();
    rotate::reset(g_rotate, millis());

    // Idle dim init: start at full brightness; loop tracks last activity.
    M5.Display.setBrightness(BACKLIGHT_FULL);
    g_last_activity_ms = millis();
    g_last_page_swap_ms = millis();

    // Stub state for visual verification before first BLE payload arrives.
    g_state.claude = { 71, 134, 38, 6240, "allow", true };
    g_state.codex  = { 0, 0, 0, 0, "unavailable", false };
    g_state.focus.agent = data::AgentKind::Claude;
    std::strcpy(g_state.focus.repo, "rotator");
    g_state.focus.sessions = 2;
    g_state.initialized = true;

    ble_peer::begin([](const data::PayloadState& parsed) {
        g_state = parsed;
    });
    // HID disabled temporarily — adding HID after GATT overflows the 31-byte
    // advertising packet on NimBLE, which silently drops the device name and
    // makes the daemon's scan filter fail to match. Plan #4 adds it back with
    // scan-response splitting.
    // ble_hid::begin();
    ble_peer::request_refresh();  // ask daemon for current state on boot
}

void loop() {
    M5.update();
    poll_swipe();  // own swipe detector — LVGL gestures unreliable at 50ms cadence
    buttons::tick(g_state.focus.agent);

    uint32_t now = millis();

    // Bezel BtnA/BtnC swipe + long-press BtnB battery detail.
    int swipe_intent = buttons::consume_swipe_intent();
    if (swipe_intent < 0) {
        ui_pager::on_swipe_left(g_pager);
        g_last_page_swap_ms = now;
        mark_activity();
    } else if (swipe_intent > 0) {
        ui_pager::on_swipe_right(g_pager);
        g_last_page_swap_ms = now;
        mark_activity();
    }

    // BtnB: short tap toggles splash, long-press (>800ms) shows battery
    // detail overlay. consume_splash_toggle returns true on release (short
    // tap pattern); pressedFor(800) catches the held-down state.
    if (M5.BtnB.pressedFor(800)) {
        show_battery_overlay();
        // Drain the "toggle on release" flag so the splash doesn't also fire.
        (void)buttons::consume_splash_toggle();
        mark_activity();
    } else if (buttons::consume_splash_toggle()) {
        if (splash::is_visible()) {
            splash::hide();
        } else {
            bool on_codex =
                (ui_pager::current(g_pager) == ui_pager::Page::Codex);
            splash::show(on_codex);
        }
        mark_activity();
    }

    // Battery overlay auto-hide.
    if (g_battery_overlay_hide_at_ms != 0 && now >= g_battery_overlay_hide_at_ms) {
        hide_battery_overlay();
        g_battery_overlay_hide_at_ms = 0;
    }

    lv_tick_inc(50);
    lv_timer_handler();

    splash::tick(millis());

    bool show_7d = (rotate::current(g_rotate, millis()) == rotate::Frame::SevenDay);
    ui_meter::refresh(g_meter, g_state, show_7d);

    // Auto-rotate countdown: thin bar drains over PAGE_SWAP_INTERVAL_MS.
    // Width = 320 * (1 - elapsed/total); when elapsed reaches total, swap
    // page and reset the timer.
    uint32_t elapsed = now - g_last_page_swap_ms;
    if (elapsed >= PAGE_SWAP_INTERVAL_MS) {
        ui_pager::on_swipe_left(g_pager);
        g_last_page_swap_ms = now;
        elapsed = 0;
    }
    if (g_rotate_hint) {
        int32_t hint_w = 320 - (int32_t)(320 * elapsed / PAGE_SWAP_INTERVAL_MS);
        if (hint_w < 0) hint_w = 0;
        lv_obj_set_width(g_rotate_hint, hint_w);
    }

    // Idle dim: drop backlight after IDLE_DIM_MS without activity. Touch
    // input via the swipe detector marks activity on press; buttons too.
    if (!g_dimmed && (now - g_last_activity_ms) > IDLE_DIM_MS) {
        M5.Display.setBrightness(BACKLIGHT_DIM);
        g_dimmed = true;
    }

    delay(50);
}

#endif  // !UNIT_TEST
// Host build: no main() here. Each `firmware/test/*/*.cpp` provides its own
// main() that drives Unity. The native env excludes main.cpp + ui_meter.cpp
// via build_src_filter so device-only headers (M5Unified.h, lvgl.h) never get
// pulled into the host compile.
