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
