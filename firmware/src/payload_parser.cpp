#include "payload_parser.h"
#include <ArduinoJson.h>
#include <cstring>

namespace parser {

static void copy_safe(char* dst, size_t cap, const char* src) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

static data::AgentKind classify(const char* s) {
    if (!s) return data::AgentKind::None;
    if (std::strcmp(s, "claude") == 0) return data::AgentKind::Claude;
    if (std::strcmp(s, "codex") == 0) return data::AgentKind::Codex;
    return data::AgentKind::None;
}

static void parse_provider(JsonObjectConst obj, data::ProviderBlock& out) {
    out.s  = obj["s"]  | 0;
    out.sr = obj["sr"] | 0;
    out.w  = obj["w"]  | 0;
    out.wr = obj["wr"] | 0;
    out.ok = obj["ok"] | false;
    copy_safe(out.st, sizeof(out.st), obj["st"] | "unknown");
}

bool parse_payload(const char* json, size_t len, data::PayloadState& out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) {
        return false;
    }
    parse_provider(doc["claude"].as<JsonObjectConst>(), out.claude);
    parse_provider(doc["codex"].as<JsonObjectConst>(), out.codex);

    auto focus = doc["focus"].as<JsonObjectConst>();
    out.focus.agent = classify(focus["agent"] | "none");
    copy_safe(out.focus.repo, sizeof(out.focus.repo), focus["repo"] | "");
    out.focus.sessions = focus["sessions"] | 0;

    out.initialized = true;
    return true;
}

}  // namespace parser
