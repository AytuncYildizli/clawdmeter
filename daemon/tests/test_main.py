import asyncio
import json
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch
import pytest
from clawdmeter.main import Orchestrator
from clawdmeter.state import State
from clawdmeter.account_pool import Account


def _fake_pool(accounts: list[Account], active_id: str | None = None):
    """Build a MagicMock that quacks like an AccountPool."""
    pool = MagicMock()
    pool.snapshot_current = MagicMock(side_effect=lambda *a, **kw: next(
        (acc for acc in accounts if acc.id == active_id), accounts[0] if accounts else None))
    pool.list_accounts = MagicMock(return_value=accounts)
    pool.active_id = active_id if active_id else (accounts[0].id if accounts else None)
    pool.get_active = MagicMock(side_effect=lambda: next(
        (a for a in accounts if a.id == pool.active_id), None))
    return pool


@pytest.mark.asyncio
async def test_orchestrator_initial_payload_contains_all_blocks():
    """After one tick the writer must receive a payload with active claude
    block + accounts list + codex stub + focus."""
    sent_payloads = []

    async def fake_write(payload):
        sent_payloads.append(payload)

    writer = MagicMock()
    writer.write_payload = AsyncMock(side_effect=fake_write)
    writer.run = AsyncMock()
    writer.on_refresh = None
    writer.stop = MagicMock()

    accounts = [Account(id="aabb1122", access_token="t1", refresh_token="r1",
                        expires_at_ms=10**15, label="primary")]
    fake_block = {"s": 50, "sr": 60, "w": 25, "wr": 4000, "st": "allow", "ok": True}
    fake_focus = MagicMock()
    fake_focus.to_dict.return_value = {"agent": "claude", "repo": "rotator", "sessions": 1}

    codex_stub_block = {"s": 0, "sr": 0, "w": 0, "wr": 0, "st": "unavailable", "ok": False}
    with patch("clawdmeter.main.AccountPool",
               return_value=_fake_pool(accounts, "aabb1122")), \
         patch("clawdmeter.main.probe_claude", return_value=fake_block), \
         patch("clawdmeter.main.probe_codex", return_value=codex_stub_block), \
         patch("clawdmeter.main.read_focus", return_value=fake_focus):
        orch = Orchestrator(writer=writer, poll_interval=0.01, debounce=0.01)
        task = asyncio.create_task(orch.run())
        await asyncio.sleep(0.05)
        orch.stop()
        await asyncio.sleep(0.02)
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass

    assert len(sent_payloads) >= 1
    payload = sent_payloads[-1]
    assert payload["claude"]["s"] == 50
    assert payload["codex"]["ok"] is False
    assert payload["focus"]["agent"] == "claude"
    assert len(payload["claude_accounts"]) == 1
    assert payload["claude_accounts"][0]["a"] is True
    assert payload["claude_accounts"][0]["s"] == 50


@pytest.mark.asyncio
async def test_orchestrator_two_accounts_marks_only_active():
    """Two accounts in the pool: only one carries a=True (active flag)."""
    sent_payloads = []
    writer = MagicMock()
    writer.write_payload = AsyncMock(side_effect=lambda p: sent_payloads.append(p))
    writer.run = AsyncMock()
    writer.on_refresh = None
    writer.stop = MagicMock()

    accounts = [
        Account(id="aaaa1111", access_token="t1", refresh_token="r1",
                expires_at_ms=10**15, label="acct-aaaa"),
        Account(id="bbbb2222", access_token="t2", refresh_token="r2",
                expires_at_ms=10**15, label="acct-bbbb"),
    ]

    def fake_probe(token, now_epoch=None):
        return {"s": 70 if token == "t1" else 10,
                "sr": 0, "w": 50 if token == "t1" else 5, "wr": 0,
                "st": "allow", "ok": True}

    fake_focus = MagicMock()
    fake_focus.to_dict.return_value = {"agent": "claude", "repo": "x", "sessions": 0}

    with patch("clawdmeter.main.AccountPool",
               return_value=_fake_pool(accounts, "aaaa1111")), \
         patch("clawdmeter.main.probe_claude", side_effect=fake_probe), \
         patch("clawdmeter.main.read_focus", return_value=fake_focus):
        orch = Orchestrator(writer=writer, poll_interval=0.01, debounce=0.01)
        task = asyncio.create_task(orch.run())
        await asyncio.sleep(0.05)
        orch.stop()
        await asyncio.sleep(0.02)
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass

    assert len(sent_payloads) >= 1
    rows = sent_payloads[-1]["claude_accounts"]
    assert len(rows) == 2
    active_rows = [r for r in rows if r["a"]]
    assert len(active_rows) == 1
    assert active_rows[0]["s"] == 70  # t1 -> 70%


@pytest.mark.asyncio
async def test_orchestrator_skips_probe_for_expired_token():
    """An account whose access_token has expired must NOT call probe_claude
    for that token (would only return 401 anyway). Pool entry still ships
    on the wire with ok=False."""
    sent_payloads = []
    writer = MagicMock()
    writer.write_payload = AsyncMock(side_effect=lambda p: sent_payloads.append(p))
    writer.run = AsyncMock()
    writer.on_refresh = None
    writer.stop = MagicMock()

    accounts = [
        Account(id="aaaa1111", access_token="t1", refresh_token="r1",
                expires_at_ms=10**15, label="active"),
        # Expired account
        Account(id="bbbb2222", access_token="t2-expired", refresh_token="r2",
                expires_at_ms=0, label="expired"),
    ]

    probed_tokens = []

    def fake_probe(token, now_epoch=None):
        probed_tokens.append(token)
        return {"s": 30, "sr": 0, "w": 15, "wr": 0, "st": "allow", "ok": True}

    fake_focus = MagicMock()
    fake_focus.to_dict.return_value = {"agent": "claude", "repo": "x", "sessions": 0}

    with patch("clawdmeter.main.AccountPool",
               return_value=_fake_pool(accounts, "aaaa1111")), \
         patch("clawdmeter.main.probe_claude", side_effect=fake_probe), \
         patch("clawdmeter.main.read_focus", return_value=fake_focus):
        orch = Orchestrator(writer=writer, poll_interval=0.01, debounce=0.01)
        task = asyncio.create_task(orch.run())
        await asyncio.sleep(0.05)
        orch.stop()
        await asyncio.sleep(0.02)
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass

    # Only the active (non-expired) token was probed
    assert "t1" in probed_tokens
    assert "t2-expired" not in probed_tokens
    # Both accounts on the wire; expired one marked ok=False
    rows = sent_payloads[-1]["claude_accounts"]
    expired_row = next(r for r in rows if r["n"] == "expired")
    assert expired_row["ok"] is False


@pytest.mark.asyncio
async def test_refresh_callback_triggers_immediate_poll():
    """When BleWriter fires on_refresh, the orchestrator polls Claude again."""
    poll_count = 0

    def counting_probe(token, now_epoch=None):
        nonlocal poll_count
        poll_count += 1
        return {"s": 0, "sr": 0, "w": 0, "wr": 0, "st": "unknown", "ok": False}

    writer = MagicMock()
    writer.write_payload = AsyncMock()
    writer.run = AsyncMock()
    writer.stop = MagicMock()
    writer.on_refresh = None

    accounts = [Account(id="aaaa1111", access_token="t", refresh_token="r",
                        expires_at_ms=10**15)]
    fake_focus = MagicMock()
    fake_focus.to_dict.return_value = {"agent": "none", "repo": "", "sessions": 0}

    with patch("clawdmeter.main.AccountPool",
               return_value=_fake_pool(accounts, "aaaa1111")), \
         patch("clawdmeter.main.probe_claude", side_effect=counting_probe), \
         patch("clawdmeter.main.read_focus", return_value=fake_focus):
        orch = Orchestrator(writer=writer, poll_interval=10.0, debounce=0.01)
        task = asyncio.create_task(orch.run())
        await asyncio.sleep(0.05)
        baseline = poll_count
        writer.on_refresh()
        await asyncio.sleep(0.05)
        assert poll_count > baseline
        orch.stop()
        await asyncio.sleep(0.02)
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass
