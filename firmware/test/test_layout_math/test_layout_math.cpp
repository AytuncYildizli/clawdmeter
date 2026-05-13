#include <unity.h>
#include "layout_math.h"

void test_big_number_centered() {
    // 320×240 screen. A glyph of width 120 should land at x = (320-120)/2 = 100
    auto pos = layout::big_number_origin(120, 96);
    TEST_ASSERT_EQUAL_INT(100, pos.x);
    // Vertical centering: y = (240-96)/2 = 72
    TEST_ASSERT_EQUAL_INT(72, pos.y);
}

void test_provider_tag_top_centered() {
    auto pos = layout::provider_tag_origin(80, 11);
    TEST_ASSERT_EQUAL_INT(120, pos.x);  // (320-80)/2
    TEST_ASSERT_EQUAL_INT(8, pos.y);    // fixed top margin per spec
}

void test_page_indicator_position() {
    auto pos = layout::page_indicator_origin();
    // Bottom-center, 4px from bottom edge
    TEST_ASSERT_EQUAL_INT(236, pos.y);  // 240 - 4
}

void test_repo_label_position_above_indicator() {
    auto pos = layout::repo_label_origin(100, 12);
    TEST_ASSERT_EQUAL_INT(110, pos.x);  // (320-100)/2
    TEST_ASSERT_TRUE(pos.y > 200 && pos.y < 230);  // between meter and dots
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_big_number_centered);
    RUN_TEST(test_provider_tag_top_centered);
    RUN_TEST(test_page_indicator_position);
    RUN_TEST(test_repo_label_position_above_indicator);
    return UNITY_END();
}
