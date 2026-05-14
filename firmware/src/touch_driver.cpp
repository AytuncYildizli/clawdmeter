#include "touch_driver.h"

#include <Arduino.h>
#include <Wire.h>

namespace touch {

namespace {

constexpr uint8_t FT_ADDR = 0x38;
constexpr uint8_t REG_TD_STATUS = 0x02;
constexpr uint8_t REG_P1_XH = 0x03;  // event flag (bits 7:6) + X high nibble (bits 3:0)
// REG_P1_XL = 0x04, REG_P1_YH = 0x05, REG_P1_YL = 0x06

// Panel native dimensions (portrait, before display rotation).
constexpr int16_t NATIVE_W = 240;
[[maybe_unused]] constexpr int16_t NATIVE_H = 320;
// Display dimensions after setRotation(1).
constexpr int16_t LAND_W = 320;
constexpr int16_t LAND_H = 240;

bool g_pressed = false;
bool g_pending_press = false;  // set true on press-down edge, cleared by consume
int16_t g_x = 0;
int16_t g_y = 0;

}  // namespace

bool update() {
    // Read TD_STATUS + first-finger registers (0x02..0x06) in one transaction.
    Wire1.beginTransmission(FT_ADDR);
    Wire1.write(REG_TD_STATUS);
    if (Wire1.endTransmission(false) != 0) {
        g_pressed = false;
        return false;
    }
    constexpr uint8_t N = 5;  // td + xH + xL + yH + yL
    Wire1.requestFrom(FT_ADDR, N);
    uint8_t buf[N] = {0};
    size_t got = 0;
    while (Wire1.available() && got < N) buf[got++] = Wire1.read();
    if (got < N) {
        g_pressed = false;
        return false;
    }

    uint8_t td = buf[0];
    uint8_t xH = buf[1];
    uint8_t xL = buf[2];
    uint8_t yH = buf[3];
    uint8_t yL = buf[4];

    // Event flag in xH[7:6]:
    //   0b00 = press down (new touch)
    //   0b01 = lift up
    //   0b10 = contact (held)
    //   0b11 = no event / reserved
    uint8_t event = (xH >> 6) & 0x03;
    bool is_pressed = (td > 0) && (event == 0x00 || event == 0x02);

    if (is_pressed) {
        int16_t native_x = ((xH & 0x0F) << 8) | xL;
        int16_t native_y = ((yH & 0x0F) << 8) | yL;
        // 90 deg clockwise rotation: landscape_x = native_y; landscape_y = NATIVE_W - native_x
        g_x = native_y;
        g_y = NATIVE_W - native_x;
        // Clamp to landscape bounds — guards against off-screen edge noise.
        if (g_x < 0) g_x = 0;
        if (g_x >= LAND_W) g_x = LAND_W - 1;
        if (g_y < 0) g_y = 0;
        if (g_y >= LAND_H) g_y = LAND_H - 1;
        if (!g_pressed && event == 0x00) {
            g_pending_press = true;
        }
        g_pressed = true;
        return true;
    } else {
        g_pressed = false;
        return false;
    }
}

int16_t x() { return g_x; }
int16_t y() { return g_y; }

bool consume_press_event() {
    if (g_pending_press) {
        g_pending_press = false;
        return true;
    }
    return false;
}

}  // namespace touch
