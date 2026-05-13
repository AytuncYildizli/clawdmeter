import asyncio
import json
from unittest.mock import AsyncMock, MagicMock, patch
import pytest
from clawdmeter.ble_writer import (
    BleWriter,
    SERVICE_UUID,
    RX_CHAR_UUID,
    REQ_CHAR_UUID,
    DEVICE_NAME,
)


def test_ble_uuids_match_spec():
    """Spec § 8.1 — these UUIDs are the public contract with the firmware."""
    assert SERVICE_UUID == "4c41555a-4465-7669-6365-000000000001"
    assert RX_CHAR_UUID == "4c41555a-4465-7669-6365-000000000002"
    assert REQ_CHAR_UUID == "4c41555a-4465-7669-6365-000000000004"
    assert DEVICE_NAME == "Clawd Controller"


@pytest.mark.asyncio
async def test_writer_serializes_and_writes_payload():
    """The writer must JSON-encode the payload and call write_gatt_char on RX."""
    mock_client = MagicMock()
    mock_client.write_gatt_char = AsyncMock()
    mock_client.is_connected = True

    writer = BleWriter()
    writer._client = mock_client  # injected for testing

    payload = {"claude": {"s": 71, "ok": True}, "codex": {"ok": False}, "focus": {"agent": "claude"}}
    await writer.write_payload(payload)

    mock_client.write_gatt_char.assert_awaited_once()
    args, kwargs = mock_client.write_gatt_char.call_args
    # First positional or kwarg: the characteristic UUID
    char_arg = args[0] if args else kwargs.get("char_specifier")
    data_arg = args[1] if len(args) > 1 else kwargs.get("data")
    assert str(char_arg) == RX_CHAR_UUID
    assert json.loads(data_arg.decode("utf-8")) == payload


@pytest.mark.asyncio
async def test_writer_skips_when_disconnected():
    """If no client is connected, write_payload must not raise."""
    writer = BleWriter()
    writer._client = None
    # Should silently no-op
    await writer.write_payload({"any": "payload"})


@pytest.mark.asyncio
async def test_writer_refresh_callback_invoked_on_req_notify():
    """When the device writes to REQ characteristic, our callback fires."""
    writer = BleWriter()
    callback_called = asyncio.Event()
    writer.on_refresh = lambda: callback_called.set()

    # Simulate bleak invoking the notify handler
    writer._handle_req_notify(sender=MagicMock(), data=b"refresh")

    # Give event loop a tick to process
    await asyncio.sleep(0)
    assert callback_called.is_set()
