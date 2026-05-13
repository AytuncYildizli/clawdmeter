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
    assert "eyJaccess_fake_payload_fake_signature" not in text
    assert "…" in text
    assert "ChatGPT" in text  # non-secret is preserved
