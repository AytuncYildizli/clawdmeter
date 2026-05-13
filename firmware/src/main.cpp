#ifndef UNIT_TEST

#include <M5Unified.h>

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setCursor(10, 10);
    M5.Display.println("Clawdmeter v0.1");
    M5.Display.println("Boot OK");
}

void loop() {
    M5.update();
    delay(50);
}

#endif  // !UNIT_TEST
// Host build: no main() here. Each `firmware/test/*/*.cpp` provides its own
// main() that drives Unity. `test_build_src = yes` pulls src/*.cpp into the
// test binary so parsers, layout math, etc. link in.
