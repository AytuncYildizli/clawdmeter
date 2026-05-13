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
