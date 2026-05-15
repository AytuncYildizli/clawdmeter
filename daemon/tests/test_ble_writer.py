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

    # Small payload -> single chunk with marker=0x02 (last/only).
    mock_client.write_gatt_char.assert_awaited_once()
    args, kwargs = mock_client.write_gatt_char.call_args
    char_arg = args[0] if args else kwargs.get("char_specifier")
    data_arg = args[1] if len(args) > 1 else kwargs.get("data")
    assert str(char_arg) == RX_CHAR_UUID
    # Wire format: 1-byte marker + JSON. For a single chunk the marker is 0x02.
    assert data_arg[0] == 0x02
    assert json.loads(data_arg[1:].decode("utf-8")) == payload


@pytest.mark.asyncio
async def test_writer_chunks_large_payload():
    """A payload exceeding the chunk budget must split into multiple writes
    with 0x00 (first) ... 0x01 (middle) ... 0x02 (last) markers."""
    mock_client = MagicMock()
    mock_client.write_gatt_char = AsyncMock()
    mock_client.is_connected = True

    writer = BleWriter()
    writer._client = mock_client

    # Build a payload whose JSON encoding exceeds 2 * CHUNK_DATA_BUDGET so
    # we get at least 3 chunks (first + middle + last).
    big_string = "x" * (writer.CHUNK_DATA_BUDGET * 3)
    payload = {"big": big_string}
    await writer.write_payload(payload)

    calls = mock_client.write_gatt_char.call_args_list
    assert len(calls) >= 3, f"expected >=3 chunks, got {len(calls)}"

    # Markers in order: first=0x00, middles=0x01, last=0x02.
    markers = [c.args[1][0] for c in calls]
    assert markers[0] == 0x00
    assert markers[-1] == 0x02
    for m in markers[1:-1]:
        assert m == 0x01

    # Reassembling the body across chunks must equal the original JSON.
    body = b"".join(c.args[1][1:] for c in calls)
    assert json.loads(body.decode("utf-8")) == payload


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
