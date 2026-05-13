import pytest
from src.redact import redact_token, scrub_dict


def test_redact_short_token_returns_dots():
    # tokens under 12 chars get fully masked
    assert redact_token("abc") == "***"
    assert redact_token("short") == "***"
    assert redact_token("") == "***"


def test_redact_normal_token_keeps_prefix_and_suffix():
    # 6-char prefix, ellipsis, 4-char suffix
    token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.payload.signature_long_tail"
    out = redact_token(token)
    assert out.startswith("eyJhbG")
    assert out.endswith("tail")
    assert "…" in out
    # the dangerous middle is gone
    assert "payload" not in out


def test_redact_none_returns_none_marker():
    assert redact_token(None) == "<none>"


def test_scrub_dict_replaces_known_token_keys():
    payload = {
        "access_token": "eyJhbGciOiJ_dangerous_full_token_here",
        "refresh_token": "another_dangerous_full_value",
        "OPENAI_API_KEY": "sk-proj-shouldNotLeak",
        "auth_mode": "ChatGPT",
        "nested": {"id_token": "eyJanother_dangerous_token"},
    }
    scrubbed = scrub_dict(payload)
    assert "_dangerous_" not in repr(scrubbed)
    assert "shouldNotLeak" not in repr(scrubbed)
    assert scrubbed["auth_mode"] == "ChatGPT"  # non-secret preserved
    assert "…" in scrubbed["access_token"]
    assert "…" in scrubbed["nested"]["id_token"]


def test_scrub_dict_handles_authorization_headers():
    headers = {"Authorization": "Bearer eyJfull_token_value_here", "Content-Type": "application/json"}
    out = scrub_dict(headers)
    assert "eyJfull_token_value_here" not in repr(out)
    assert out["Content-Type"] == "application/json"
    # the "Bearer " prefix should be preserved for readability
    assert out["Authorization"].startswith("Bearer ")


def test_scrub_dict_does_not_mutate_input():
    payload = {"access_token": "secret_value"}
    original_repr = repr(payload)
    scrub_dict(payload)
    assert repr(payload) == original_repr
