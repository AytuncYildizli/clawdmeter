#ifndef UNIT_TEST

#include <M5Unified.h>
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

// LVGL display buffer — partial mode, 10 scanlines wide.
static lv_display_t* g_disp;
static lv_color_t g_buf1[320 * 10];

static void disp_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    int32_t w = (area->x2 - area->x1 + 1);
    int32_t h = (area->y2 - area->y1 + 1);
    // M5GFX's pushImageDMA<T> requires T to implement a get() method, which
    // LVGL 9's lv_color_t (a plain 16-bit struct) does not. Use the address-
    // window + writePixelsDMA(uint16_t*, len, swap=false) overload instead.
    // LV_COLOR_DEPTH=16 with default endianness already matches the panel's
    // RGB565 expectation, so no byte swap is required.
    M5.Display.startWrite();
    M5.Display.setAddrWindow(area->x1, area->y1, w, h);
    M5.Display.writePixelsDMA(reinterpret_cast<uint16_t*>(px_map), w * h, false);
    M5.Display.endWrite();
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
    ble_hid::begin();  // reuses NimBLE server already created by ble_peer
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

    delay(50);
}

#endif  // !UNIT_TEST
// Host build: no main() here. Each `firmware/test/*/*.cpp` provides its own
// main() that drives Unity. The native env excludes main.cpp + ui_meter.cpp
// via build_src_filter so device-only headers (M5Unified.h, lvgl.h) never get
// pulled into the host compile.
