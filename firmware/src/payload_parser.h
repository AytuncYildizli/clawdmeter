#pragma once
#include <cstddef>
#include "data.h"

namespace parser {

// Parses a JSON payload (NOT NUL-terminated required) into the PayloadState.
// Returns true on success, false on malformed JSON. On success, s.initialized
// is set to true. On failure, s is left in whatever partial state was reached.
bool parse_payload(const char* json, size_t len, data::PayloadState& out);

}  // namespace parser
