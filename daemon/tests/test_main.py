import asyncio
import json
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch
import pytest
from clawdmeter.main import Orchestrator
from clawdmeter.state import State


@pytest.mark.asyncio
async def test_orchestrator_initial_payload_contains_all_blocks():
    """After one tick of each poller, the BleWriter must receive a payload
    with claude/codex/focus blocks populated."""
    sent_payloads = []

    async def fake_write(payload):
        sent_payloads.append(payload)

    writer = MagicMock()
    writer.write_payload = AsyncMock(side_effect=fake_write)
    writer.run = AsyncMock()  # never connects in this test
    writer.on_refresh = None
    writer.stop = MagicMock()

    fake_claude_block = {"s": 50, "sr": 60, "w": 25, "wr": 4000, "st": "allow", "ok": True}
    fake_focus = MagicMock()
    fake_focus.to_dict.return_value = {"agent": "claude", "repo": "rotator", "sessions": 1}

    with patch("clawdmeter.main.probe_claude", return_value=fake_claude_block), \
         patch("clawdmeter.main.read_focus", return_value=fake_focus), \
         patch("clawdmeter.main.load_claude_token", return_value="fake-token"):
        orch = Orchestrator(writer=writer, poll_interval=0.01, debounce=0.01)
        # Run for ~30ms — long enough for one full poll + write
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
    writer.on_refresh = None  # orchestrator will set this

    with patch("clawdmeter.main.probe_claude", side_effect=counting_probe), \
         patch("clawdmeter.main.read_focus", return_value=MagicMock(to_dict=lambda: {"agent": "none", "repo": "", "sessions": 0})), \
         patch("clawdmeter.main.load_claude_token", return_value="t"):
        orch = Orchestrator(writer=writer, poll_interval=10.0, debounce=0.01)
        task = asyncio.create_task(orch.run())
        await asyncio.sleep(0.05)
        # poll_count should be 1 (initial)
        baseline = poll_count
        # Trigger refresh
        writer.on_refresh()
        await asyncio.sleep(0.05)
        # poll_count should now be at least baseline + 1
        assert poll_count > baseline
        orch.stop()
        await asyncio.sleep(0.02)
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass
