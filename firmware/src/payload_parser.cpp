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

    // Multi-account pool — read up to MAX_CLAUDE_ACCOUNTS entries.
    // Keys follow the wire-compact shape: n, s, sr, w, wr, ok, a.
    out.claude_account_count = 0;
    auto accounts_arr = doc["claude_accounts"].as<JsonArrayConst>();
    if (!accounts_arr.isNull()) {
        for (JsonObjectConst row : accounts_arr) {
            if (out.claude_account_count >= data::MAX_CLAUDE_ACCOUNTS) break;
            auto& slot = out.claude_accounts[out.claude_account_count];
            copy_safe(slot.name, sizeof(slot.name), row["n"] | "");
            slot.s  = row["s"]  | 0;
            slot.sr = row["sr"] | 0;
            slot.w  = row["w"]  | 0;
            slot.wr = row["wr"] | 0;
            slot.ok = row["ok"] | false;
            slot.active = row["a"] | false;
            out.claude_account_count++;
        }
    }

    // Activity feed — read up to MAX_ACTIVITY_EVENTS entries (newest-first).
    // Wire shape: {t: epoch, v: "+/-/>/=", a: "c/x/n", r: "repo"}
    out.activity_event_count = 0;
    auto events_arr = doc["activity_events"].as<JsonArrayConst>();
    if (!events_arr.isNull()) {
        for (JsonObjectConst row : events_arr) {
            if (out.activity_event_count >= data::MAX_ACTIVITY_EVENTS) break;
            auto& slot = out.activity_events[out.activity_event_count];
            slot.ts_epoch = row["t"] | 0u;
            const char* v = row["v"] | " ";
            slot.verb = v[0];  // single-char verb
            const char* a = row["a"] | "n";
            slot.agent = a[0];
            copy_safe(slot.repo, sizeof(slot.repo), row["r"] | "");
            out.activity_event_count++;
        }
    }

    out.initialized = true;
    return true;
}

}  // namespace parser
