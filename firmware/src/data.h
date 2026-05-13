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

struct PayloadState {
    ProviderBlock claude;
    ProviderBlock codex;
    FocusBlock focus;
    bool initialized = false;  // becomes true after the first successful parse
};

}  // namespace data
