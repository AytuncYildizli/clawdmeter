#include <unity.h>
#include <cstring>
#include "data.h"
#include "payload_parser.h"

// The exact JSON shape produced by daemon's State.to_payload()
const char* SAMPLE_JSON = R"({
    "claude": {"s":71,"sr":134,"w":38,"wr":6240,"st":"allow","ok":true},
    "codex":  {"s":0,"sr":0,"w":0,"wr":0,"st":"unavailable","ok":false},
    "focus":  {"agent":"claude","repo":"rotator","sessions":3}
})";

void test_parses_full_payload() {
    data::PayloadState s{};
    bool ok = parser::parse_payload(SAMPLE_JSON, strlen(SAMPLE_JSON), s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(71, s.claude.s);
    TEST_ASSERT_EQUAL_INT(134, s.claude.sr);
    TEST_ASSERT_EQUAL_INT(38, s.claude.w);
    TEST_ASSERT_EQUAL_INT(6240, s.claude.wr);
    TEST_ASSERT_EQUAL_STRING("allow", s.claude.st);
    TEST_ASSERT_TRUE(s.claude.ok);
    TEST_ASSERT_FALSE(s.codex.ok);
    TEST_ASSERT_EQUAL_STRING("unavailable", s.codex.st);
    TEST_ASSERT_EQUAL(data::AgentKind::Claude, s.focus.agent);
    TEST_ASSERT_EQUAL_STRING("rotator", s.focus.repo);
    TEST_ASSERT_EQUAL_INT(3, s.focus.sessions);
    TEST_ASSERT_TRUE(s.initialized);
}

void test_focus_agent_codex() {
    const char* j = R"({"claude":{"ok":false},"codex":{"ok":false},"focus":{"agent":"codex","repo":"katman","sessions":0}})";
    data::PayloadState s{};
    parser::parse_payload(j, strlen(j), s);
    TEST_ASSERT_EQUAL(data::AgentKind::Codex, s.focus.agent);
}

void test_focus_agent_none() {
    const char* j = R"({"claude":{"ok":false},"codex":{"ok":false},"focus":{"agent":"none","repo":"","sessions":0}})";
    data::PayloadState s{};
    parser::parse_payload(j, strlen(j), s);
    TEST_ASSERT_EQUAL(data::AgentKind::None, s.focus.agent);
}

void test_unknown_agent_defaults_to_none() {
    const char* j = R"({"claude":{"ok":false},"codex":{"ok":false},"focus":{"agent":"mastra","repo":"x","sessions":0}})";
    data::PayloadState s{};
    parser::parse_payload(j, strlen(j), s);
    TEST_ASSERT_EQUAL(data::AgentKind::None, s.focus.agent);
}

void test_malformed_json_returns_false() {
    const char* j = "{not valid";
    data::PayloadState s{};
    bool ok = parser::parse_payload(j, strlen(j), s);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_FALSE(s.initialized);
}

void test_long_repo_name_truncated_safely() {
    const char* j = R"({"claude":{},"codex":{},"focus":{"agent":"claude","repo":"a_very_long_repo_name_that_definitely_exceeds_the_buffer","sessions":0}})";
    data::PayloadState s{};
    parser::parse_payload(j, strlen(j), s);
    // Buffer is 32 chars including NUL — accept anything up to 31 chars in the field
    TEST_ASSERT_TRUE(strlen(s.focus.repo) < 32);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_full_payload);
    RUN_TEST(test_focus_agent_codex);
    RUN_TEST(test_focus_agent_none);
    RUN_TEST(test_unknown_agent_defaults_to_none);
    RUN_TEST(test_malformed_json_returns_false);
    RUN_TEST(test_long_repo_name_truncated_safely);
    return UNITY_END();
}
