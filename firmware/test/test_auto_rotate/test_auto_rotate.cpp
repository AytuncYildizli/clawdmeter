#include <unity.h>
#include "auto_rotate.h"

void test_starts_on_5h() {
    rotate::State st{};
    rotate::reset(st, 0);
    TEST_ASSERT_EQUAL(rotate::Frame::FiveHour, rotate::current(st, 0));
}

void test_flips_at_5_seconds() {
    rotate::State st{};
    rotate::reset(st, 1000);  // start at t=1000ms
    TEST_ASSERT_EQUAL(rotate::Frame::FiveHour, rotate::current(st, 1000));
    TEST_ASSERT_EQUAL(rotate::Frame::FiveHour, rotate::current(st, 4999));
    TEST_ASSERT_EQUAL(rotate::Frame::SevenDay, rotate::current(st, 6000));
    TEST_ASSERT_EQUAL(rotate::Frame::SevenDay, rotate::current(st, 10999));
    TEST_ASSERT_EQUAL(rotate::Frame::FiveHour, rotate::current(st, 11000));
}

void test_reset_resyncs_to_5h() {
    rotate::State st{};
    rotate::reset(st, 0);
    // Advance into 7d region
    rotate::current(st, 7000);
    // Reset at a new t0
    rotate::reset(st, 10000);
    TEST_ASSERT_EQUAL(rotate::Frame::FiveHour, rotate::current(st, 10000));
    TEST_ASSERT_EQUAL(rotate::Frame::SevenDay, rotate::current(st, 15000));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_on_5h);
    RUN_TEST(test_flips_at_5_seconds);
    RUN_TEST(test_reset_resyncs_to_5h);
    return UNITY_END();
}
