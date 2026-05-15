#pragma once
#include <cstdint>

namespace data {

enum class AgentKind : uint8_t {
    None,
    Claude,
    Codex,
};

struct ProviderBlock {
    int s = 0;         // 5h utilization percent (0-100)
    int sr = 0;        // minutes until 5h reset
    int w = 0;         // 7d utilization percent
    int wr = 0;        // minutes until 7d reset
    char st[16] = {};  // "allow" | "warn" | "block" | "unknown" | "unavailable"
    bool ok = false;
};

struct FocusBlock {
    AgentKind agent = AgentKind::None;
    char repo[32] = {};   // worktree-derived project name
    int sessions = 0;     // count of other active agent panes
};

// Compact per-account row sent in the BLE payload. Names truncated to 12 chars
// + NUL — the daemon emits "acct-aabb" style identifiers or user-set labels.
// The active flag mirrors which account is currently used by Claude Code.
struct AccountSummary {
    char name[13] = {};
    int s = 0;          // 5h utilization percent
    int sr = 0;         // minutes until 5h reset
    int w = 0;          // 7d utilization percent
    int wr = 0;         // minutes until 7d reset
    bool ok = false;
    bool active = false;
};

constexpr int MAX_CLAUDE_ACCOUNTS = 3;

struct PayloadState {
    ProviderBlock claude;
    ProviderBlock codex;
    FocusBlock focus;
    // Multi-account roll-up. count = number of populated entries (0..MAX).
    // Daemon ships up to 3 in priority order; firmware renders all of them.
    AccountSummary claude_accounts[MAX_CLAUDE_ACCOUNTS];
    int claude_account_count = 0;
    bool initialized = false;  // becomes true after the first successful parse
    // millis() timestamp of the most recent BLE payload write. 0 = never.
    // Used by the UI freshness footer to render Synced/Stale/Offline. Set
    // by the BLE onWrite callback, not parsed from the payload.
    uint32_t last_payload_millis = 0;
};

}  // namespace data
