# Codex Probe Research Spike — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Determine whether the macOS Clawdmeter daemon can read Codex usage from a ChatGPT-mode authenticated session, and if so, produce a tested parser ready to plug into the daemon plan.

**Architecture:** A small, self-contained Python spike under `research/codex-spike/`. Loads OAuth tokens from `~/.codex/auth.json`, probes candidate `chatgpt.com/backend-api/*` endpoints in capture mode (saving raw responses to fixtures), then writes TDD'd parsers against the captured fixtures. Output is either (a) a tested parser function ready for the daemon, or (b) a documented dead-end with a stub recommendation.

**Tech Stack:** Python 3.10+, `pytest`, standard library `urllib.request` + `json` (no async, no extra HTTP deps for this spike).

---

## Spec references

- Source spec: `docs/superpowers/specs/2026-05-13-clawdmeter-core2-port-design.md`
- Section 7.4 (Codex probe) — the failure-mode that motivates this spike
- Section 11 (Open items, #1) — research item carried into the plan
- Spec payload contract: `codex` dict needs `{s, sr, w, wr, st, ok}`; if no working endpoint is found, `ok=false` is acceptable

## Working assumptions (verify in Task 1)

- `~/.codex/auth.json` exists with keys `auth_mode`, `OPENAI_API_KEY`, `tokens`, `last_refresh`
- `auth_mode == "ChatGPT"` for the user running this spike
- `tokens` contains an access token and/or refresh token usable as a Bearer credential against `chatgpt.com/backend-api/*`

If any assumption fails, Task 1 records the failure and halts the spike with a "cannot probe" report.

## Security rules (apply to every task)

1. **Never log full OAuth tokens.** All logging must redact tokens to `<prefix>…<suffix>` (first 6 + last 4 chars).
2. **Never commit raw auth.json or captured responses that contain tokens.** Captured fixtures must be scrubbed before commit (see Task 4).
3. **Never echo Bearer headers to terminal output.** Use `repr()` only on redacted values.

## File structure

```
research/codex-spike/
├── README.md                 # how to run the spike + reproduce findings
├── REPORT.md                 # written in Task 8; the spike's primary deliverable
├── requirements.txt          # pytest only
├── pytest.ini                # discovery config
├── src/
│   ├── __init__.py
│   ├── auth.py               # load tokens from auth.json (redacted logging)
│   ├── redact.py             # token-redaction helpers (security-critical, isolated)
│   ├── probe.py              # endpoint probe runner with capture mode
│   ├── candidates.py         # list of endpoints to try
│   └── parsers/
│       ├── __init__.py
│       └── (one .py per candidate that returned useful data)
├── fixtures/
│   ├── auth-sample.json      # synthetic auth.json for testing auth.py
│   └── responses/            # captured live responses (SCRUBBED before commit)
└── tests/
    ├── test_redact.py
    ├── test_auth.py
    ├── test_probe.py         # uses recorded fixtures, no live network
    └── test_parsers.py       # one test per candidate parser
```

---

### Task 1: Spike workspace setup

**Files:**
- Create: `research/codex-spike/README.md`
- Create: `research/codex-spike/requirements.txt`
- Create: `research/codex-spike/pytest.ini`
- Create: `research/codex-spike/src/__init__.py`
- Create: `research/codex-spike/src/parsers/__init__.py`
- Create: `research/codex-spike/tests/__init__.py`
- Modify: `.gitignore` (add fixture-raw guard)

- [ ] **Step 1: Create the directory tree**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
mkdir -p research/codex-spike/src/parsers
mkdir -p research/codex-spike/tests
mkdir -p research/codex-spike/fixtures/responses
touch research/codex-spike/src/__init__.py
touch research/codex-spike/src/parsers/__init__.py
touch research/codex-spike/tests/__init__.py
```

- [ ] **Step 2: Add requirements.txt**

Write `research/codex-spike/requirements.txt`:

```
pytest>=8.0
```

- [ ] **Step 3: Add pytest.ini**

Write `research/codex-spike/pytest.ini`:

```ini
[pytest]
testpaths = tests
python_files = test_*.py
addopts = -ra -q
```

- [ ] **Step 4: Add README.md (operational doc)**

Write `research/codex-spike/README.md`:

```markdown
# Codex Probe Research Spike

Determines whether Codex usage can be read from a ChatGPT-mode authenticated
session. Output is `REPORT.md` plus a tested parser (if a working endpoint
was found) or a stub-fallback recommendation.

## Setup

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## Run tests

```bash
pytest
```

## Run a live probe (uses real ~/.codex/auth.json)

```bash
python -m src.probe --capture
```

This writes raw responses to `fixtures/responses/<endpoint-slug>.json`.

## Security

This spike never logs or commits raw OAuth tokens. Captured fixtures are
scrubbed automatically by `src/redact.py` before being written to disk.
If you ever see a fixture file containing `eyJ` or a long Bearer-looking
string, do not commit it — re-run with capture mode and report the bug.
```

- [ ] **Step 5: Add .gitignore protection for raw captures**

Modify `.gitignore` — append:

```
# Codex spike: never commit unsanitized captures
research/codex-spike/fixtures/responses/*.raw.json
research/codex-spike/.venv/
research/codex-spike/__pycache__/
research/codex-spike/**/__pycache__/
```

- [ ] **Step 6: Verify pytest can discover the empty test dir**

```bash
cd research/codex-spike && python3 -m pytest --collect-only
```

Expected output includes: `no tests ran` or `collected 0 items`. No errors. If pytest is not installed, install it via `pip install pytest` and retry.

- [ ] **Step 7: Commit**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git add research/codex-spike/ .gitignore
git commit -m "spike(codex): scaffold research workspace"
```

---

### Task 2: Token redaction helper (security-critical, isolated module)

**Files:**
- Create: `research/codex-spike/src/redact.py`
- Test: `research/codex-spike/tests/test_redact.py`

**Why isolated:** Redaction is security-critical. Keeping it in its own module with comprehensive tests means later code can rely on it without re-verifying.

Matching is **case-insensitive** on both dict keys and the Bearer scheme so that lowercase headers from real HTTP libraries (httpx returns lowercase header names) and lowercase-variant secret keys captured from API responses are still scrubbed.

- [ ] **Step 1: Write the failing test**

Write `research/codex-spike/tests/test_redact.py`:

```python
import pytest
from src.redact import redact_token, scrub_dict, scrub_text, scrub_any


def test_redact_short_token_returns_dots():
    # tokens under 12 chars get fully masked
    assert redact_token("abc") == "***"
    assert redact_token("short") == "***"
    assert redact_token("") == "***"


def test_redact_twelve_char_token_is_redacted_not_masked():
    # boundary: 12-char value is the smallest "normal" token
    out = redact_token("abcdefghijkl")  # exactly 12
    assert "…" in out
    assert out != "***"
    assert out.startswith("abcdef")
    assert out.endswith("ijkl")


def test_redact_normal_token_keeps_prefix_and_suffix():
    token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.payload.signature_long_tail"
    out = redact_token(token)
    assert out.startswith("eyJhbG")
    assert out.endswith("tail")
    assert "…" in out
    # the dangerous middle is gone — STRONG INVARIANT
    assert "payload" not in out
    assert "signature_long_tail" not in out or out.endswith("tail")  # only the 4-char suffix is allowed
    # the full token must not appear in the output
    assert token not in out


def test_redact_none_returns_none_marker():
    assert redact_token(None) == "<none>"


def test_redact_non_string_returns_non_str_marker():
    assert redact_token(12345) == "<non-str>"
    assert redact_token(["a", "b"]) == "<non-str>"


def test_scrub_dict_replaces_known_token_keys_and_invariant():
    full_access = "eyJhbGciOiJ_dangerous_full_token_here"
    full_refresh = "another_dangerous_full_refresh_value"
    full_openai = "sk-proj-shouldNotLeak_full_value"
    full_nested = "eyJanother_dangerous_id_token_value"
    payload = {
        "access_token": full_access,
        "refresh_token": full_refresh,
        "OPENAI_API_KEY": full_openai,
        "auth_mode": "ChatGPT",
        "nested": {"id_token": full_nested},
    }
    scrubbed = scrub_dict(payload)
    # STRONG INVARIANT: no original secret appears anywhere in the scrubbed output
    rendered = repr(scrubbed)
    assert full_access not in rendered
    assert full_refresh not in rendered
    assert full_openai not in rendered
    assert full_nested not in rendered
    # non-secret preserved
    assert scrubbed["auth_mode"] == "ChatGPT"
    # redaction marker present
    assert "…" in scrubbed["access_token"]
    assert "…" in scrubbed["nested"]["id_token"]


def test_scrub_dict_handles_authorization_headers_case_insensitive():
    full = "eyJfull_token_value_here_long"
    # title-case "Authorization", title-case "Bearer"
    h1 = scrub_dict({"Authorization": f"Bearer {full}"})
    # lowercase "authorization", lowercase "bearer" — common from httpx
    h2 = scrub_dict({"authorization": f"bearer {full}"})
    # mixed case
    h3 = scrub_dict({"AUTHORIZATION": f"BEARER {full}"})
    for h, key in [(h1, "Authorization"), (h2, "authorization"), (h3, "AUTHORIZATION")]:
        rendered = repr(h)
        assert full not in rendered, f"leak in {key!r} variant: {rendered!r}"
        # scheme preserved (case may match input or be normalized; just check non-empty)
        assert h[key].lower().startswith("bearer ")


def test_scrub_dict_preserves_non_secret_headers():
    out = scrub_dict({"Content-Type": "application/json", "X-Request-Id": "req-1"})
    assert out["Content-Type"] == "application/json"
    assert out["X-Request-Id"] == "req-1"


def test_scrub_dict_lowercase_secret_keys_are_caught():
    full = "eyJlowercase_secret_key_full_value"
    out = scrub_dict({
        "access_token": full,         # exact match
        "Refresh_Token": full + "1",  # mixed-case
        "openai_api_key": full + "2", # lowercase env-style
    })
    rendered = repr(out)
    assert full not in rendered
    assert (full + "1") not in rendered
    assert (full + "2") not in rendered


def test_scrub_dict_deeply_nested():
    full = "eyJdeeply_nested_token_value"
    payload = {
        "level1": {
            "level2": {
                "level3": {
                    "access_token": full
                }
            }
        }
    }
    out = scrub_dict(payload)
    assert full not in repr(out)
    assert "…" in out["level1"]["level2"]["level3"]["access_token"]


def test_scrub_dict_list_of_dicts():
    full = "eyJlist_dict_token_value_full"
    payload = {"sessions": [{"access_token": full}, {"id_token": full + "x"}]}
    out = scrub_dict(payload)
    rendered = repr(out)
    assert full not in rendered
    assert (full + "x") not in rendered


def test_scrub_dict_additional_secret_keys():
    full = "verylongsecretvaluefull"
    payload = {
        "cookie": full,
        "set-cookie": full + "1",
        "password": full + "2",
        "client_secret": full + "3",
    }
    out = scrub_dict(payload)
    rendered = repr(out)
    for s in [full, full + "1", full + "2", full + "3"]:
        assert s not in rendered, f"leak: {s!r} found in {rendered!r}"


def test_scrub_dict_does_not_mutate_input():
    payload = {"access_token": "secret_value_long_enough"}
    original_repr = repr(payload)
    scrub_dict(payload)
    assert repr(payload) == original_repr


def test_scrub_text_redacts_bearer_in_freeform_text():
    leak = "eyJleaked_jwt_value_here_long_enough_to_match"
    text = f"Forbidden: token Bearer {leak} is invalid"
    out = scrub_text(text)
    assert leak not in out
    # The "Bearer X" mask should fire
    assert "Bearer <redacted>" in out


def test_scrub_text_redacts_jwt_in_html_body():
    leak = "eyJabcdefghijklmnopqrstuvwxyz0123456789.payload.signature"
    html = f"<html><body>session={leak}</body></html>"
    out = scrub_text(html)
    assert leak not in out
    assert "<jwt-redacted>" in out


def test_scrub_any_handles_list_of_dicts_at_top_level():
    leak = "eyJlist_top_level_token_value"
    payload = [{"access_token": leak}, {"plan": "plus"}]
    out = scrub_any(payload)
    rendered = repr(out)
    assert leak not in rendered
    # non-secret preserved
    assert out[1]["plan"] == "plus"


def test_scrub_any_passes_through_non_collection_non_string():
    assert scrub_any(42) == 42
    assert scrub_any(None) is None
    assert scrub_any(True) is True
    assert scrub_any(3.14) == 3.14
```

- [ ] **Step 2: Run the test, verify it fails**

```bash
cd research/codex-spike && python3 -m pytest tests/test_redact.py -v
```

Expected: `ModuleNotFoundError: No module named 'src.redact'` or `ImportError`.

- [ ] **Step 3: Implement the minimal module**

Write `research/codex-spike/src/redact.py`:

```python
"""Token redaction. Security-critical; do not modify without re-running tests.

Policy (case-insensitive on both keys and the Bearer scheme):
- Keys whose lowered form is in SECRET_KEYS get their string values masked
- Values matching the Bearer scheme (any case) have only their credential redacted
- Non-secret keys pass through unchanged
- Input is never mutated; a new dict is returned

`scrub_text` and `scrub_any` extend coverage to freeform text and
arbitrary JSON-shaped values (dict | list | str | scalar) so that
non-JSON response bodies and top-level JSON arrays/scalars also get
scrubbed before being persisted to disk.
"""
from __future__ import annotations
import re
from typing import Any

# Use lowercased keys — membership tests lowercase the candidate key first.
SECRET_KEYS = {
    "access_token", "refresh_token", "id_token",
    "openai_api_key", "api_key", "token", "tokens", "authorization",
    "cookie", "set-cookie", "password", "client_secret",
}


def redact_token(value: str | None) -> str:
    """Mask a token, keeping enough to identify it without exposing it."""
    if value is None:
        return "<none>"
    if not isinstance(value, str):
        return "<non-str>"
    if len(value) < 12:
        return "***"
    return f"{value[:6]}…{value[-4:]}"


def _is_secret_key(key: str) -> bool:
    return isinstance(key, str) and key.lower() in SECRET_KEYS


def _redact_bearer(value: str) -> str:
    """If `value` looks like an HTTP auth scheme + credential, redact only the credential.
    Returns the value unchanged if no recognizable scheme is found."""
    if not isinstance(value, str):
        return value
    parts = value.split(" ", 1)
    if len(parts) == 2 and parts[0].lower() == "bearer":
        return f"{parts[0]} {redact_token(parts[1])}"
    # Unknown scheme — redact the whole thing rather than leak it
    return redact_token(value)


def _scrub_value(key: str, value: Any) -> Any:
    if isinstance(value, dict):
        return scrub_dict(value)
    if isinstance(value, list):
        return [_scrub_value(key, v) for v in value]
    if _is_secret_key(key) and isinstance(value, str):
        # authorization header gets scheme-preserving treatment
        if key.lower() == "authorization":
            return _redact_bearer(value)
        return redact_token(value)
    return value


def scrub_dict(data: dict) -> dict:
    """Return a copy of `data` with secrets redacted. Recurses into nested dicts/lists."""
    result: dict = {}
    for k, v in data.items():
        result[k] = _scrub_value(k, v)
    return result


# Regex patterns for freeform-text scrubbing. Order matters: the full
# three-part JWT pattern must run before the shorter JWT-prefix pattern.
_TEXT_PATTERNS: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"Bearer\s+\S+", re.IGNORECASE), "Bearer <redacted>"),
    (re.compile(r"eyJ[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+"), "<jwt-redacted>"),
    (re.compile(r"eyJ[A-Za-z0-9_-]{20,}"), "<jwt-redacted>"),
    (re.compile(r"sk-(?:proj-)?[A-Za-z0-9]{20,}"), "<openai-key-redacted>"),
    (re.compile(r"(?im)^Set-Cookie:.*$"), "Set-Cookie: <redacted>"),
]


def scrub_text(text: str) -> str:
    """Mask token-shaped substrings in freeform text. Used for non-JSON
    response bodies (HTML error pages, plain-text 401/403 messages)."""
    if not isinstance(text, str):
        return text
    out = text
    for pat, repl in _TEXT_PATTERNS:
        out = pat.sub(repl, out)
    return out


def scrub_any(value: Any) -> Any:
    """Top-level dispatch for any JSON-shaped value.

    - dict  → scrub_dict (recursive)
    - list  → recurse element by element
    - str   → scrub_text (regex masks)
    - other → returned unchanged
    """
    if isinstance(value, dict):
        return scrub_dict(value)
    if isinstance(value, list):
        return [scrub_any(v) for v in value]
    if isinstance(value, str):
        return scrub_text(value)
    return value
```

- [ ] **Step 4: Run the test, verify it passes**

```bash
cd research/codex-spike && python3 -m pytest tests/test_redact.py -v
```

Expected: all 17 tests pass.

- [ ] **Step 5: Commit**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git add research/codex-spike/src/redact.py research/codex-spike/tests/test_redact.py
git commit -m "spike(codex): token redaction helper"
```

---

### Task 3: Auth loader

**Files:**
- Create: `research/codex-spike/src/auth.py`
- Create: `research/codex-spike/fixtures/auth-sample.json`
- Test: `research/codex-spike/tests/test_auth.py`

- [ ] **Step 1: Create the synthetic auth fixture**

Write `research/codex-spike/fixtures/auth-sample.json`:

```json
{
  "auth_mode": "ChatGPT",
  "OPENAI_API_KEY": "sk-proj-fake",
  "tokens": {
    "access_token": "eyJaccess_fake_payload_fake_signature",
    "refresh_token": "eyJrefresh_fake_payload_fake_signature",
    "id_token": "eyJid_fake_payload_fake_signature"
  },
  "last_refresh": "2026-05-13T08:00:00Z"
}
```

- [ ] **Step 2: Write the failing test**

Write `research/codex-spike/tests/test_auth.py`:

```python
import json
from pathlib import Path
import pytest
from src.auth import CodexAuth, load_auth, AuthError

FIXTURES = Path(__file__).parent.parent / "fixtures"


def test_load_chatgpt_mode_from_fixture():
    auth = load_auth(FIXTURES / "auth-sample.json")
    assert auth.mode == "ChatGPT"
    assert auth.access_token == "eyJaccess_fake_payload_fake_signature"
    assert auth.refresh_token == "eyJrefresh_fake_payload_fake_signature"
    assert auth.id_token == "eyJid_fake_payload_fake_signature"


def test_load_missing_file_raises():
    with pytest.raises(AuthError, match="not found"):
        load_auth(Path("/nonexistent/auth.json"))


def test_load_malformed_json_raises(tmp_path):
    bad = tmp_path / "bad.json"
    bad.write_text("{not valid json")
    with pytest.raises(AuthError, match="parse"):
        load_auth(bad)


def test_load_api_key_mode_marked_unsupported(tmp_path):
    apikey_file = tmp_path / "apikey.json"
    apikey_file.write_text(json.dumps({
        "auth_mode": "ApiKey",
        "OPENAI_API_KEY": "sk-proj-xxx",
        "tokens": {},
        "last_refresh": None,
    }))
    with pytest.raises(AuthError, match="auth_mode='ApiKey'"):
        load_auth(apikey_file)


def test_redacted_repr_is_safe():
    auth = load_auth(FIXTURES / "auth-sample.json")
    text = repr(auth)
    # All three token fields must be redacted in repr
    assert "eyJaccess_fake_payload_fake_signature" not in text
    assert "eyJrefresh_fake_payload_fake_signature" not in text
    assert "eyJid_fake_payload_fake_signature" not in text
    assert "…" in text
    assert "ChatGPT" in text  # non-secret is preserved


def test_load_missing_tokens_dict_treats_tokens_as_none():
    """Edge case: auth.json with no `tokens` key should yield CodexAuth
    with all-None token fields, not raise."""
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
        json.dump({"auth_mode": "ChatGPT", "last_refresh": None}, f)
        path = Path(f.name)
    try:
        auth = load_auth(path)
        assert auth.mode == "ChatGPT"
        assert auth.access_token is None
        assert auth.refresh_token is None
        assert auth.id_token is None
    finally:
        path.unlink()


def test_load_empty_tokens_dict_yields_none_fields(tmp_path):
    """Edge case: tokens={} should produce all-None token fields."""
    f = tmp_path / "empty_tokens.json"
    f.write_text(json.dumps({"auth_mode": "ChatGPT", "tokens": {}, "last_refresh": None}))
    auth = load_auth(f)
    assert auth.access_token is None
    assert auth.refresh_token is None
    assert auth.id_token is None
```

- [ ] **Step 3: Run the test, verify it fails**

```bash
cd research/codex-spike && python3 -m pytest tests/test_auth.py -v
```

Expected: `ModuleNotFoundError: No module named 'src.auth'`.

- [ ] **Step 4: Implement the loader**

Write `research/codex-spike/src/auth.py`:

```python
"""Loader for ~/.codex/auth.json. Refuses to operate on non-ChatGPT modes
(the daemon's spec only covers ChatGPT-mode probing).

WARNING: ``dataclasses.asdict()`` and ``CodexAuth.__dict__`` bypass the
redacting ``__repr__`` and expose raw tokens. Never log via those — use
``repr()`` or address fields explicitly.
"""
from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
import json
from .redact import redact_token


class AuthError(Exception):
    """Raised when auth state is missing, malformed, or unsupported."""


@dataclass
class CodexAuth:
    mode: str
    access_token: str | None
    refresh_token: str | None
    id_token: str | None
    last_refresh: str | None

    def __repr__(self) -> str:
        return (
            f"CodexAuth(mode={self.mode!r}, "
            f"access_token={redact_token(self.access_token)}, "
            f"refresh_token={redact_token(self.refresh_token)}, "
            f"id_token={redact_token(self.id_token)}, "
            f"last_refresh={self.last_refresh!r})"
        )


def load_auth(path: Path) -> CodexAuth:
    if not path.exists():
        raise AuthError(f"auth file not found: {path}")
    try:
        data = json.loads(path.read_text())
    except json.JSONDecodeError as e:
        # Generic message — JSONDecodeError.str can surface bytes around the
        # parse failure, which for ~/.codex/auth.json may include token chars.
        # `from e` keeps the original exception in the traceback chain for
        # debug-mode tracebacks without exposing it in the public message.
        raise AuthError("failed to parse auth file") from e

    mode = data.get("auth_mode")
    # Strict compare: Codex writes "ChatGPT" literally. If the format ever drifts
    # (whitespace, casing), fail loud rather than paper over upstream changes.
    if mode != "ChatGPT":
        raise AuthError(f"unsupported auth_mode={mode!r}; this spike only handles ChatGPT")

    tokens = data.get("tokens") or {}
    return CodexAuth(
        mode=mode,
        access_token=tokens.get("access_token"),
        refresh_token=tokens.get("refresh_token"),
        id_token=tokens.get("id_token"),
        last_refresh=data.get("last_refresh"),
    )
```

- [ ] **Step 5: Run the test, verify it passes**

```bash
cd research/codex-spike && python3 -m pytest tests/test_auth.py -v
```

Expected: all 7 tests pass.

- [ ] **Step 6: Commit**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git add research/codex-spike/src/auth.py research/codex-spike/tests/test_auth.py research/codex-spike/fixtures/auth-sample.json
git commit -m "spike(codex): auth loader with ChatGPT-mode gating"
```

---

### Task 4: Endpoint candidates list

**Files:**
- Create: `research/codex-spike/src/candidates.py`

This is plain data — no tests needed. The probe runner (Task 5) will iterate over this list.

- [ ] **Step 1: Write the candidates module**

Write `research/codex-spike/src/candidates.py`:

```python
"""Candidate endpoints to probe for ChatGPT-mode usage data.

Each entry: (slug, method, url, notes).

Slugs are lowercase-kebab and become fixture filenames: e.g.
'conversation-limit' -> fixtures/responses/conversation-limit.json
"""
from __future__ import annotations
from dataclasses import dataclass


@dataclass(frozen=True)
class Candidate:
    slug: str
    method: str
    url: str
    notes: str


CANDIDATES: list[Candidate] = [
    Candidate(
        slug="conversation-limit",
        method="GET",
        url="https://chatgpt.com/backend-api/conversation_limit",
        notes="Top guess: name implies plan limit / usage state",
    ),
    Candidate(
        slug="models-usage",
        method="GET",
        url="https://chatgpt.com/backend-api/models/usage",
        notes="Plausible: matches OpenAI's per-model usage shape",
    ),
    Candidate(
        slug="me",
        method="GET",
        url="https://chatgpt.com/backend-api/me",
        notes="Account info — may include plan + limits",
    ),
    Candidate(
        slug="account-check",
        method="GET",
        url="https://chatgpt.com/backend-api/accounts/check",
        notes="Known account-status endpoint; may carry plan tier",
    ),
    Candidate(
        slug="account-billing",
        method="GET",
        url="https://chatgpt.com/backend-api/accounts/check/v4-2023-04-27",
        notes="Versioned account check — known to return subscription info",
    ),
]
```

- [ ] **Step 2: Commit**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git add research/codex-spike/src/candidates.py
git commit -m "spike(codex): candidate endpoint list"
```

---

### Task 5: Probe runner (capture mode)

**Files:**
- Create: `research/codex-spike/src/probe.py`
- Test: `research/codex-spike/tests/test_probe.py`

The probe runner has two modes: `dry-run` (uses a mock URL opener — tested) and `capture` (real network, manual exploration).

- [ ] **Step 1: Write the failing test (dry-run mode)**

Write `research/codex-spike/tests/test_probe.py`:

```python
import json
from pathlib import Path
from unittest.mock import patch
import pytest
from src.probe import probe_one, ProbeResult
from src.candidates import Candidate


def test_probe_one_records_status_and_body():
    candidate = Candidate(slug="test", method="GET", url="https://example.com/test", notes="")
    fake_response = (200, {"content-type": "application/json"}, b'{"hello":"world"}')

    def fake_request(method, url, headers):
        assert method == "GET"
        assert url == "https://example.com/test"
        assert headers["Authorization"].startswith("Bearer ")
        return fake_response

    with patch("src.probe._do_request", fake_request):
        result = probe_one(candidate, token="fake-access-token")

    assert result.slug == "test"
    assert result.status == 200
    assert result.body == {"hello": "world"}
    assert result.error is None


def test_probe_one_handles_non_json_body():
    candidate = Candidate(slug="html", method="GET", url="https://example.com", notes="")
    fake_response = (200, {"content-type": "text/html"}, b"<html></html>")

    with patch("src.probe._do_request", lambda m, u, h: fake_response):
        result = probe_one(candidate, token="fake")

    assert result.status == 200
    assert result.body is None
    assert result.raw_body == "<html></html>"


def test_probe_one_records_errors():
    candidate = Candidate(slug="oops", method="GET", url="https://example.com/oops", notes="")

    def boom(m, u, h):
        raise OSError("network down")

    with patch("src.probe._do_request", boom):
        result = probe_one(candidate, token="fake")

    assert result.status is None
    assert result.error is not None
    assert "network down" in result.error


def test_save_capture_writes_scrubbed_json(tmp_path):
    from src.probe import save_capture
    result = ProbeResult(
        slug="acct",
        status=200,
        headers={"Authorization": "Bearer eyJfull_value_here", "X-Request-Id": "req-1"},
        body={"plan": "plus", "access_token": "eyJleaked_here"},
        raw_body=None,
        error=None,
    )
    save_capture(result, tmp_path)
    written = json.loads((tmp_path / "acct.json").read_text())
    text = json.dumps(written)
    assert "eyJfull_value_here" not in text
    assert "eyJleaked_here" not in text
    assert written["status"] == 200
    assert written["body"]["plan"] == "plus"


def test_save_capture_scrubs_raw_body(tmp_path):
    """Regression: non-JSON response bodies (HTML/plain text) must not
    leak token-shaped substrings to disk."""
    from src.probe import save_capture
    leak = "eyJleaked_jwt_value_here_long_enough"
    result = ProbeResult(
        slug="raw",
        status=401,
        headers={},
        body=None,
        raw_body=f"Forbidden: token Bearer {leak} is invalid",
        error=None,
    )
    save_capture(result, tmp_path)
    written = json.loads((tmp_path / "raw.json").read_text())
    text = json.dumps(written)
    # STRONG INVARIANT: the leaked secret string must not appear on disk
    assert leak not in text
    assert "Bearer <redacted>" in written["raw_body"]


def test_save_capture_scrubs_list_body(tmp_path):
    """Regression: JSON bodies that are top-level lists (or scalars)
    must still be scrubbed — not skipped by a dict-only guard."""
    from src.probe import save_capture
    leak = "eyJlist_top_level_token_value_full"
    result = ProbeResult(
        slug="list_body",
        status=200,
        headers={},
        body=[{"access_token": leak}, {"plan": "plus"}],
        raw_body=None,
        error=None,
    )
    save_capture(result, tmp_path)
    written = json.loads((tmp_path / "list_body.json").read_text())
    text = json.dumps(written)
    assert leak not in text
    # Non-secret content preserved
    assert written["body"][1]["plan"] == "plus"
```

- [ ] **Step 2: Run the test, verify it fails**

```bash
cd research/codex-spike && python3 -m pytest tests/test_probe.py -v
```

Expected: `ModuleNotFoundError: No module named 'src.probe'`.

- [ ] **Step 3: Implement the probe runner**

Write `research/codex-spike/src/probe.py`:

```python
"""Probe runner. Two responsibilities:
1. `probe_one(candidate, token)` — issue one request, return structured result
2. `save_capture(result, dir)` — write a SCRUBBED capture to fixtures/

CLI: `python -m src.probe --capture` iterates all CANDIDATES against the
live ~/.codex/auth.json and saves captures for manual inspection.
"""
from __future__ import annotations
import argparse
import json
import sys
import urllib.request
import urllib.error
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any
from .auth import load_auth
from .candidates import CANDIDATES, Candidate
from .redact import scrub_any, scrub_text


@dataclass
class ProbeResult:
    slug: str
    status: int | None
    headers: dict[str, str] = field(default_factory=dict)
    body: dict | None = None
    raw_body: str | None = None
    error: str | None = None


def _do_request(method: str, url: str, headers: dict[str, str]) -> tuple[int, dict[str, str], bytes]:
    """Network round-trip. Patched out in tests."""
    req = urllib.request.Request(url, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return resp.status, dict(resp.headers), resp.read()
    except urllib.error.HTTPError as e:
        # HTTPError has the same interface as the response
        return e.code, dict(e.headers), e.read()


def probe_one(candidate: Candidate, token: str) -> ProbeResult:
    headers = {
        "Authorization": f"Bearer {token}",
        "Accept": "application/json",
        "User-Agent": "clawdmeter-spike/0.1 (research)",
    }
    try:
        status, resp_headers, raw = _do_request(candidate.method, candidate.url, headers)
    except Exception as e:
        return ProbeResult(slug=candidate.slug, status=None, error=str(e))

    body: dict | None = None
    raw_body: str | None = None
    try:
        body = json.loads(raw.decode("utf-8"))
    except Exception:
        raw_body = raw.decode("utf-8", errors="replace")[:4000]

    return ProbeResult(
        slug=candidate.slug,
        status=status,
        headers={k: v for k, v in resp_headers.items()},
        body=body,
        raw_body=raw_body,
        error=None,
    )


def save_capture(result: ProbeResult, out_dir: Path) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"{result.slug}.json"
    payload = asdict(result)
    # Scrub before writing — every field that can carry secrets.
    # - headers: dict, may contain Authorization etc.
    # - body: arbitrary JSON shape (dict, list, scalar)
    # - raw_body: freeform text (HTML, plain-text 401/403). Token-shaped
    #   substrings get regex-masked. The 4000-char cap on raw_body is a
    #   readability limit, NOT a security control — JWTs fit in <4000.
    payload["headers"] = scrub_any(payload["headers"])
    payload["body"] = scrub_any(payload["body"])
    if payload["raw_body"] is not None:
        payload["raw_body"] = scrub_text(payload["raw_body"])
    out_path.write_text(json.dumps(payload, indent=2, sort_keys=True))
    return out_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture", action="store_true", help="Run live and save fixtures")
    parser.add_argument("--auth", default=str(Path.home() / ".codex" / "auth.json"))
    parser.add_argument("--out", default=str(Path(__file__).parent.parent / "fixtures" / "responses"))
    args = parser.parse_args()

    if not args.capture:
        print("--capture required for live probe", file=sys.stderr)
        return 2

    auth = load_auth(Path(args.auth))
    print(f"Loaded auth: {auth!r}")
    if not auth.access_token:
        print("No access_token in auth.json — cannot probe", file=sys.stderr)
        return 1

    out_dir = Path(args.out)
    for c in CANDIDATES:
        print(f"\n→ {c.slug}  {c.method} {c.url}")
        result = probe_one(c, auth.access_token)
        path = save_capture(result, out_dir)
        if result.error:
            print(f"  error: {result.error}")
        else:
            print(f"  status={result.status}  body_keys={list(result.body.keys()) if isinstance(result.body, dict) else 'non-json'}")
        print(f"  capture: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Run the test, verify it passes**

```bash
cd research/codex-spike && python3 -m pytest tests/test_probe.py -v
```

Expected: all 6 tests pass.

- [ ] **Step 5: Commit**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git add research/codex-spike/src/probe.py research/codex-spike/tests/test_probe.py
git commit -m "spike(codex): probe runner with scrubbed capture mode"
```

---

### Task 6: Live capture (exploratory — manual judgment required)

**Not a TDD task.** This task is the empirical heart of the spike. The engineer runs the live probe and inspects the results.

**Files:**
- Will create: `research/codex-spike/fixtures/responses/*.json` (one per candidate, scrubbed)

- [ ] **Step 1: Confirm prerequisites**

```bash
ls ~/.codex/auth.json
python3 -c "import json; d=json.load(open('/Users/aytuncyildizli/.codex/auth.json')); print('auth_mode:', d.get('auth_mode'))"
```

Expected: `auth.json` exists, `auth_mode: ChatGPT`. If not ChatGPT, halt and write Task 8 report explaining the spike is not applicable to the current setup.

- [ ] **Step 2: Run the live probe**

```bash
cd research/codex-spike
python3 -m src.probe --capture
```

Expected: prints status + body_keys for each candidate, writes one file to `fixtures/responses/` per candidate.

- [ ] **Step 3: Inspect each capture for usage data**

For each file in `research/codex-spike/fixtures/responses/`, open it and look for:

- Numeric fields suggesting utilization, rate, quota, used, remaining
- Timestamps suggesting a reset window (epoch seconds or ISO-8601)
- Plan-tier identifiers (`plus`, `pro`, `team`)
- HTTP status: 200 = candidate; 401 = token rejected; 403 = endpoint restricted; 404 = wrong URL

```bash
for f in research/codex-spike/fixtures/responses/*.json; do
  echo "=== $f ==="
  python3 -c "
import json
d = json.load(open('$f'))
print('status:', d['status'])
if isinstance(d['body'], dict):
    print('body keys:', list(d['body'].keys()))
elif d.get('error'):
    print('error:', d['error'])
"
done
```

- [ ] **Step 4: Decision gate**

Categorize each candidate as one of:
- **WINNER** — returned plan-utilization data the daemon can use
- **PARTIAL** — returned account info but no rate/usage numbers
- **TOKEN-REJECTED** — 401; access_token wasn't accepted (may need different token, may need session cookie)
- **NOT-FOUND** — 404
- **OTHER** — write the actual status + brief note

Write the categorization into a temporary note. If you have at least one WINNER, proceed to Task 7. If you have only PARTIAL/REJECTED/NOT-FOUND, proceed directly to Task 8 (stub fallback report).

- [ ] **Step 5: Commit captures**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git add research/codex-spike/fixtures/responses/
git status
```

**Before committing, re-confirm scrubbing:** run

```bash
grep -rE "eyJ[A-Za-z0-9_-]{20,}" research/codex-spike/fixtures/responses/ || echo "SCRUBBED OK"
grep -rE "Bearer [A-Za-z0-9._-]{20,}" research/codex-spike/fixtures/responses/ || echo "SCRUBBED OK"
```

Both must print "SCRUBBED OK". If either prints a match, **STOP**. Re-run the probe (the scrubber failed) and report the bug. Do NOT commit unsanitized captures.

If clean:

```bash
git commit -m "spike(codex): capture live responses from $(ls research/codex-spike/fixtures/responses/ | wc -l | tr -d ' ') candidates"
```

---

### Task 7: Parser for the winning endpoint (skip if no winner)

**Files (depend on the winner — adapt slug):**
- Create: `research/codex-spike/src/parsers/<slug>.py`
- Test: `research/codex-spike/tests/test_parsers.py`

**If Task 6 found no winner, skip directly to Task 8.**

The parser must extract the spec's required shape:

```python
{
  "s":  int,    # 5h-equivalent utilization percent (0-100)
  "sr": int,    # minutes until 5h-equivalent reset
  "w":  int,    # 7d-equivalent utilization percent
  "wr": int,    # minutes until 7d-equivalent reset
  "st": str,    # "allow" | "warn" | "block" | "unknown"
  "ok": True,
}
```

If the winning endpoint exposes only one timeframe instead of two, map it to `s/sr` and leave `w/wr=0`. If it exposes a different timeframe (e.g., monthly), map to whichever (`s` or `w`) feels closest and document in REPORT.md.

- [ ] **Step 1: Write the failing parser test against the captured fixture**

(Example assumes the winner was `conversation-limit` with a hypothetical response shape — adapt slug and field names based on what was actually captured.)

Append to `research/codex-spike/tests/test_parsers.py`:

```python
import json
from pathlib import Path
import pytest
from src.parsers.conversation_limit import parse, ParseError

FIXTURES = Path(__file__).parent.parent / "fixtures" / "responses"


def test_parse_conversation_limit_extracts_usage():
    fixture_path = FIXTURES / "conversation-limit.json"
    if not fixture_path.exists():
        pytest.skip("conversation-limit fixture not present (Task 6 did not capture)")
    body = json.loads(fixture_path.read_text())["body"]
    result = parse(body)
    # Adapt these asserts to whatever the actual response shape was
    assert 0 <= result["s"] <= 100
    assert result["sr"] >= 0
    assert result["st"] in {"allow", "warn", "block", "unknown"}
    assert result["ok"] is True


def test_parse_missing_fields_returns_unknown():
    result = parse({"unrelated": "value"})
    assert result["ok"] is False or result["st"] == "unknown"


def test_parse_malformed_input_raises():
    with pytest.raises(ParseError):
        parse(None)
    with pytest.raises(ParseError):
        parse("not a dict")
```

- [ ] **Step 2: Run the test, verify it fails**

```bash
cd research/codex-spike && python3 -m pytest tests/test_parsers.py -v
```

Expected: `ModuleNotFoundError: No module named 'src.parsers.conversation_limit'`.

- [ ] **Step 3: Implement the parser**

Adapt the field names to the actual response shape captured in Task 6. Skeleton template:

Write `research/codex-spike/src/parsers/conversation_limit.py`:

```python
"""Parser for the /conversation_limit response shape.

Maps the ChatGPT-mode response into the daemon's payload contract.
ADAPT the field-extraction logic to match what Task 6 captured.
"""
from __future__ import annotations
from typing import Any


class ParseError(Exception):
    pass


def _coerce_pct(value: Any) -> int:
    if isinstance(value, (int, float)):
        # response may be 0..1 (ratio) or 0..100 (percent) — heuristic:
        v = float(value)
        if v <= 1.0:
            return int(round(v * 100))
        return int(round(v))
    raise ParseError(f"cannot coerce {value!r} to percent")


def _coerce_reset_minutes(value: Any) -> int:
    # If the response gives an epoch seconds reset time, convert to mins from now.
    # If it gives a duration in seconds/minutes/hours, normalize to minutes.
    # Adapt this once the shape is known.
    if isinstance(value, (int, float)):
        return max(0, int(value // 60))  # default assumption: epoch seconds delta-from-now
    return 0


def parse(body: Any) -> dict:
    if not isinstance(body, dict):
        raise ParseError(f"expected dict, got {type(body).__name__}")

    # ADAPT: these key names are placeholders. Replace with the real keys
    # discovered in Task 6's capture file.
    try:
        util_raw = body.get("limit_pct_used") or body.get("usage_pct") or body.get("used_ratio")
        reset_raw = body.get("reset_at") or body.get("reset_seconds")
    except Exception as e:
        raise ParseError(f"unexpected structure: {e}") from e

    if util_raw is None:
        return {"s": 0, "sr": 0, "w": 0, "wr": 0, "st": "unknown", "ok": False}

    return {
        "s": _coerce_pct(util_raw),
        "sr": _coerce_reset_minutes(reset_raw) if reset_raw is not None else 0,
        "w": 0,
        "wr": 0,
        "st": "allow",
        "ok": True,
    }
```

- [ ] **Step 4: Run the test, verify it passes**

```bash
cd research/codex-spike && python3 -m pytest tests/test_parsers.py -v
```

If tests fail because the captured field names don't match the parser's placeholders, fix the parser — that's the point of this task. Iterate until the test passes against the real captured fixture.

- [ ] **Step 5: Commit**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git add research/codex-spike/src/parsers/ research/codex-spike/tests/test_parsers.py
git commit -m "spike(codex): parser for winning endpoint with fixture-based tests"
```

---

### Task 8: Write REPORT.md (the spike's primary deliverable)

**Files:**
- Create: `research/codex-spike/REPORT.md`

This is the document the daemon plan will read. It must be complete enough that someone reading only this file can decide whether to ship Codex tracking or stub.

- [ ] **Step 1: Write the report**

Adapt to actual findings. Template:

Write `research/codex-spike/REPORT.md`:

```markdown
# Codex Probe Spike — Findings

**Date:** [YYYY-MM-DD]
**Spike duration:** [N hours of probing + N hours of parser work]
**Outcome:** [WINNER FOUND | NO WORKING ENDPOINT — STUB RECOMMENDED]

## Summary

[One paragraph: what was tested, what worked, what didn't, and the recommendation for the daemon plan.]

## What was tried

| Slug | URL | Status | Outcome |
|---|---|---|---|
| conversation-limit | https://chatgpt.com/backend-api/conversation_limit | [200/401/...] | [WINNER / PARTIAL / TOKEN-REJECTED / NOT-FOUND / ERROR] |
| models-usage | https://chatgpt.com/backend-api/models/usage | ... | ... |
| me | https://chatgpt.com/backend-api/me | ... | ... |
| account-check | https://chatgpt.com/backend-api/accounts/check | ... | ... |
| account-billing | https://chatgpt.com/backend-api/accounts/check/v4-2023-04-27 | ... | ... |

## Winner [or: No winner — fallback recommendation]

### If winner found

**Endpoint:** `[URL]`
**Returns:** [describe response shape briefly]
**Mapping to daemon contract:**

| Daemon field | Source field in response | Notes |
|---|---|---|
| `codex.s` (5h util %) | [field name + transformation] | |
| `codex.sr` (5h reset min) | [field name + transformation] | |
| `codex.w` (7d util %) | [field name + transformation, or `0` if not exposed] | |
| `codex.wr` (7d reset min) | [field name + transformation, or `0` if not exposed] | |
| `codex.st` (status) | [derivation logic] | |

**Parser module:** `research/codex-spike/src/parsers/[slug].py`
**Test fixture:** `research/codex-spike/fixtures/responses/[slug].json`

**Caveats:**

- [Anything fragile about this endpoint: rate limiting on the probe itself, expected churn, token refresh requirements, etc.]
- [Recommended polling cadence — Anthropic's probe is 60s; if Codex rate-limits the probe itself, raise it.]
- [Token refresh story: does the access_token expire? Does the daemon need to refresh? If yes, link to the refresh endpoint or add a follow-up spike.]

### If no winner

**Recommendation:** Ship Codex side as a stub. Daemon writes `codex.ok = false` in every payload; firmware renders "—" on the Codex screen for utilization. Device still functions fully as a Claude-only meter, and the Codex screen still exists for swipe symmetry (just shows no-data state).

**Why each candidate failed:**

[For each candidate, one paragraph on what happened and why a fix isn't obvious. Examples: "401 with this token — likely needs a separate session cookie from chatgpt.com login flow, which the Codex CLI doesn't store" or "Returns 200 but body has no rate/usage fields — endpoint is for plan tier only."]

**Follow-up options (NOT in scope for this port):**

- Document any non-trivial paths that could be tried later (e.g., scrape `chatgpt.com` HTML, intercept Codex CLI's own usage display if it has one, watch network traffic from `codex` CLI to find the real endpoint, etc.)

## Hand-off to daemon plan

The macOS daemon plan should:

1. [If winner] Copy `src/auth.py`, `src/redact.py`, and `src/parsers/[slug].py` into the daemon source tree. Adapt the import paths but keep the test fixtures intact.
2. [If winner] Call the parser on every 60s poll cycle, write `codex` block in the BLE payload as documented in spec Section 8.2.
3. [If no winner] Stub `codex.ok = false` in every payload. Skip the Codex poller entirely. Update spec Section 7.4 to mark Codex tracking as deferred.

## Reproducing this spike

```bash
cd research/codex-spike
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
pytest                              # all parser + auth + redact tests
python -m src.probe --capture       # re-run live probe
```
```

- [ ] **Step 2: Fill out every bracketed placeholder**

Re-read the report top-to-bottom. Every `[bracketed]` placeholder must be filled with the actual finding. Empty or generic text is a plan failure.

- [ ] **Step 3: Commit**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git add research/codex-spike/REPORT.md
git commit -m "spike(codex): findings report — $(grep -m1 '^\*\*Outcome:' research/codex-spike/REPORT.md | sed 's/.*: //; s/\*\*//g')"
```

---

### Task 9: Final spike verification + close

**Files:** none (verification only)

- [ ] **Step 1: Run the full test suite**

```bash
cd research/codex-spike && python3 -m pytest -v
```

Expected: all tests pass.

- [ ] **Step 2: Confirm no secrets in tree**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
grep -rE "eyJ[A-Za-z0-9_-]{20,}" research/codex-spike/ --include='*.json' --include='*.md' --include='*.py' || echo "CLEAN"
grep -rE "sk-proj-[A-Za-z0-9]{20,}" research/codex-spike/ --include='*.json' --include='*.md' --include='*.py' || echo "CLEAN"
```

Both must print `CLEAN`. If either reports a match, redact the file (or rewrite the affected commit) before pushing.

- [ ] **Step 3: Push the branch**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git push -u origin port/core2-macos-codex
```

- [ ] **Step 4: Surface findings**

Read `research/codex-spike/REPORT.md` end-to-end. Summarize the outcome in one paragraph for the daemon-plan author:

- If winner: "Endpoint `X` returns usable data, mapped to `s/sr/w/wr` via parser `Y`. Daemon plan can proceed assuming Codex tracking is live."
- If no winner: "No reachable endpoint exposes Codex usage with the available auth state. Daemon plan should stub `codex.ok = false`."

Spike is complete. Hand back to the user with the summary.

---

## Self-review checklist (run after writing all tasks above)

- [x] Every task has explicit file paths
- [x] Every code step shows the full code, not just a description
- [x] Every test step shows the test code and the expected pass/fail outcome
- [x] Every command shows the exact invocation and the expected output
- [x] Security guardrails (token redaction, scrubbed captures) are tested, not just described
- [x] The dead-end path (Task 6 returns no winner) is explicitly handled in Task 7's skip + Task 8's fallback report
- [x] Hand-off to the daemon plan is unambiguous: REPORT.md is the contract

## Out of scope for this spike

- Token refresh logic. If the access_token expires during a 60s poll cycle, the spike does not solve it. Daemon plan will need a follow-up spike if refresh is required.
- Per-account-type variation. Spike runs against one user's auth state. If different ChatGPT plan tiers expose different endpoints/shapes, follow-up testing is needed (note in REPORT.md).
- Asynchronous probing. Spike uses blocking `urllib.request`. Daemon will move to `asyncio` + `bleak`-compatible HTTP.
