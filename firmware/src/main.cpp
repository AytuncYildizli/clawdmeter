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

#else  // UNIT_TEST — host-side native build

// Host build needs a main() entry point. The test_framework=unity machinery
// provides one when running `pio test -e native`; for `pio run -e native` we
// supply a no-op main so the build artifact (.pio/build/native/program) links.
int main(int /*argc*/, char** /*argv*/) {
    return 0;
}

#endif
