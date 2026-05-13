import json
from pathlib import Path
from unittest.mock import patch
import pytest
from clawdmeter.claude_probe import (
    parse_anthropic_headers,
    probe_claude,
    ClaudeResult,
    ClaudeProbeError,
)

FIXTURES = Path(__file__).parent.parent / "fixtures"


def test_parse_anthropic_headers_extracts_utilization():
    headers = json.loads((FIXTURES / "anthropic-200-headers.json").read_text())
    # Treat "now" as fixed for deterministic reset-minute computation
    out = parse_anthropic_headers(headers, now_epoch=1747140000)
    assert out["s"] == 71  # 0.71 * 100
    assert out["w"] == 38
    # 5h reset is at 1747144800, now is 1747140000 -> 4800s -> 80 min
    assert out["sr"] == 80
    # 7d reset is at 1747625400 -> 485400s -> 8090 min
    assert out["wr"] == 8090
    assert out["st"] == "allow"
    assert out["ok"] is True


def test_parse_anthropic_headers_handles_negative_reset():
    """Reset times in the past should clamp to 0 minutes."""
    headers = {
        "anthropic-ratelimit-unified-5h-utilization": "0.5",
        "anthropic-ratelimit-unified-5h-reset": "1000",      # very old
        "anthropic-ratelimit-unified-7d-utilization": "0.2",
        "anthropic-ratelimit-unified-7d-reset": "1000",
        "anthropic-ratelimit-unified-5h-status": "allow",
    }
    out = parse_anthropic_headers(headers, now_epoch=2000)
    assert out["sr"] == 0
    assert out["wr"] == 0


def test_parse_anthropic_headers_missing_fields_returns_zeros():
    out = parse_anthropic_headers({}, now_epoch=1747140000)
    assert out["s"] == 0
    assert out["sr"] == 0
    assert out["w"] == 0
    assert out["wr"] == 0
    assert out["st"] == "unknown"
    assert out["ok"] is False


def test_probe_claude_returns_ok_result():
    headers = json.loads((FIXTURES / "anthropic-200-headers.json").read_text())

    def fake_request(method, url, headers_in, body):
        assert method == "POST"
        assert url == "https://api.anthropic.com/v1/messages"
        assert headers_in["Authorization"].startswith("Bearer ")
        assert "anthropic-beta" in headers_in
        return (200, headers, b'{"id":"msg_x"}')

    with patch("clawdmeter.claude_probe._do_request", fake_request):
        result = probe_claude(token="fake-anthropic-token", now_epoch=1747140000)

    assert result["ok"] is True
    assert result["s"] == 71


def test_probe_claude_network_error_returns_ok_false():
    def boom(m, u, h, b):
        raise OSError("network down")

    with patch("clawdmeter.claude_probe._do_request", boom):
        result = probe_claude(token="fake", now_epoch=1747140000)

    assert result["ok"] is False
    assert result["st"] == "unknown"
