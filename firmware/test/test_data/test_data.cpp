#include <unity.h>
#include <cstring>
#include "data.h"

void test_provider_block_defaults_zero() {
    data::ProviderBlock pb;
    TEST_ASSERT_EQUAL_INT(0, pb.s);
    TEST_ASSERT_EQUAL_INT(0, pb.sr);
    TEST_ASSERT_EQUAL_INT(0, pb.w);
    TEST_ASSERT_EQUAL_INT(0, pb.wr);
    TEST_ASSERT_EQUAL_INT(0, pb.st[0]);
    TEST_ASSERT_FALSE(pb.ok);
}

void test_focus_block_defaults() {
    data::FocusBlock fb;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(data::AgentKind::None),
                            static_cast<uint8_t>(fb.agent));
    TEST_ASSERT_EQUAL_INT(0, fb.repo[0]);
    TEST_ASSERT_EQUAL_INT(0, fb.sessions);
}

void test_payload_state_uninitialized() {
    data::PayloadState ps;
    TEST_ASSERT_FALSE(ps.initialized);
    TEST_ASSERT_FALSE(ps.claude.ok);
    TEST_ASSERT_FALSE(ps.codex.ok);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(data::AgentKind::None),
                            static_cast<uint8_t>(ps.focus.agent));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_provider_block_defaults_zero);
    RUN_TEST(test_focus_block_defaults);
    RUN_TEST(test_payload_state_uninitialized);
    return UNITY_END();
}
