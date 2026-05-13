#ifndef UNIT_TEST

#include <M5Unified.h>
#include <lvgl.h>
#include <cstring>
#include "data.h"
#include "ui_meter.h"
#include "ui_pager.h"
#include "ui_ble_page.h"
#include "auto_rotate.h"
#include "ble_peer.h"
#include "ble_hid.h"
#include "buttons.h"
#include "splash.h"

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
    M5.begin(cfg);
    M5.Display.setRotation(1);  // 320×240 landscape; Core 2's ILI9342C is native portrait

    // Boot indicator — orange flash for 800ms so flashes are visually distinguishable
    M5.Display.fillScreen(M5.Display.color565(0xFF, 0x8C, 0x42));
    M5.Display.setCursor(10, 10);
    M5.Display.setTextColor(0xFFFF, M5.Display.color565(0xFF, 0x8C, 0x42));
    M5.Display.setTextSize(2);
    M5.Display.print("CLAWDMETER BOOT");
    delay(800);

    lv_init();
    g_disp = lv_display_create(320, 240);
    lv_display_set_flush_cb(g_disp, disp_flush_cb);
    lv_display_set_buffers(g_disp, g_buf1, nullptr, sizeof(g_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    g_meter = ui_meter::build(lv_scr_act());
    lv_obj_t* ble_screen = ui_ble_page::build(lv_scr_act());
    ui_pager::init(g_pager, g_meter.claude_screen, g_meter.codex_screen, ble_screen);
    // Register gesture handler on each meter screen — events don't bubble from
    // child widgets to lv_scr_act() automatically; the screen widgets themselves
    // own the touch area, so we attach there.
    lv_obj_add_event_cb(g_meter.claude_screen, screen_gesture_cb, LV_EVENT_GESTURE, nullptr);
    lv_obj_add_event_cb(g_meter.codex_screen, screen_gesture_cb, LV_EVENT_GESTURE, nullptr);
    lv_obj_add_event_cb(ble_screen, screen_gesture_cb, LV_EVENT_GESTURE, nullptr);
    lv_obj_add_event_cb(lv_scr_act(), screen_gesture_cb, LV_EVENT_GESTURE, nullptr);
    splash::init(lv_scr_act());
    rotate::reset(g_rotate, millis());

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
    buttons::tick(g_state.focus.agent);
    if (buttons::consume_splash_toggle()) {
        if (splash::is_visible()) {
            splash::hide();
        } else {
            bool on_codex =
                (ui_pager::current(g_pager) == ui_pager::Page::Codex);
            splash::show(on_codex);
        }
    }
    lv_tick_inc(50);
    lv_timer_handler();

    bool show_7d = (rotate::current(g_rotate, millis()) == rotate::Frame::SevenDay);
    ui_meter::refresh(g_meter, g_state, show_7d);
    ui_ble_page::refresh(ble_peer::is_connected());

    // Pager auto-rotation: cycle Claude -> Codex -> Bluetooth every 10s.
    // on_swipe_left already implements the wrap; reuse it so manual swipes and
    // the timer agree on direction.
    static uint32_t last_page_swap = 0;
    constexpr uint32_t PAGE_SWAP_INTERVAL = 10000;  // ms
    uint32_t now = millis();
    if (now - last_page_swap > PAGE_SWAP_INTERVAL) {
        ui_pager::on_swipe_left(g_pager);
        last_page_swap = now;
    }

    delay(50);
}

#endif  // !UNIT_TEST
// Host build: no main() here. Each `firmware/test/*/*.cpp` provides its own
// main() that drives Unity. The native env excludes main.cpp + ui_meter.cpp
// via build_src_filter so device-only headers (M5Unified.h, lvgl.h) never get
// pulled into the host compile.
