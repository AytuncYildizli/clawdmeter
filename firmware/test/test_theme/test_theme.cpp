#include <unity.h>
#include "theme.h"

void test_claude_palette_is_orange() {
    TEST_ASSERT_EQUAL_UINT32(0xFF8C42, theme::CLAUDE_ACCENT);
}

void test_codex_palette_is_teal() {
    TEST_ASSERT_EQUAL_UINT32(0x22C4A0, theme::CODEX_ACCENT);
}

void test_background_is_near_black() {
    TEST_ASSERT_EQUAL_UINT32(0x000000, theme::BG);
}

void test_dot_inactive_is_dim_gray() {
    TEST_ASSERT_EQUAL_UINT32(0x333333, theme::DOT_INACTIVE);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_claude_palette_is_orange);
    RUN_TEST(test_codex_palette_is_teal);
    RUN_TEST(test_background_is_near_black);
    RUN_TEST(test_dot_inactive_is_dim_gray);
    return UNITY_END();
}
