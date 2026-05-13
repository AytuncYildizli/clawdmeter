from clawdmeter.codex_stub import codex_stub


def test_codex_stub_always_returns_ok_false():
    result = codex_stub()
    assert result == {"s": 0, "sr": 0, "w": 0, "wr": 0, "st": "unavailable", "ok": False}


def test_codex_stub_is_idempotent():
    """Stub takes no args, returns the same value on each call."""
    assert codex_stub() == codex_stub()
