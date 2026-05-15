"""BLE central — scans for, connects to, and writes payloads to the
Clawdmeter device. Subscribes to REQ characteristic for refresh requests.

The class is designed for asyncio orchestration: `run()` is a long-running
coroutine that handles scan/connect/keep-alive, while `write_payload()`
is called by the orchestrator's debounced writer task.
"""
from __future__ import annotations
import asyncio
import json
import logging
from typing import Callable
from bleak import BleakClient, BleakScanner

# Spec § 8.1 — public contract with firmware. Do not change without firmware coordination.
SERVICE_UUID = "4c41555a-4465-7669-6365-000000000001"
RX_CHAR_UUID = "4c41555a-4465-7669-6365-000000000002"
REQ_CHAR_UUID = "4c41555a-4465-7669-6365-000000000004"
DEVICE_NAME = "Clawd Controller"

log = logging.getLogger("clawdmeter.ble")


class BleWriter:
    def __init__(self) -> None:
        self._client: BleakClient | None = None
        self._stopped = asyncio.Event()
        self.on_refresh: Callable[[], None] | None = None
        self.on_connected: Callable[[], None] | None = None

    # Chunked-write protocol — works around BLE_ATT_ATTR_MAX_LEN=512.
    # Each chunk is a single BLE write prefixed with a 1-byte marker:
    #   0x00 = first chunk of a logical payload (firmware resets buffer)
    #   0x01 = middle chunk (firmware appends to buffer)
    #   0x02 = last chunk (firmware appends then parses)
    # Chunks must arrive in order. response=True forces CoreBluetooth to
    # serialize writes per characteristic; subsequent write_gatt_char calls
    # await the ack of the previous one so order is guaranteed.
    #
    # Per-chunk data budget: ATT_MTU=517 minus 3 bytes of ATT overhead, minus
    # our 1-byte marker = 513 effective. We use 400 for generous headroom
    # against negotiated MTUs that may be smaller (some macOS releases drop
    # to 185 on first connect).
    CHUNK_DATA_BUDGET = 400

    async def write_payload(self, payload: dict) -> None:
        """Write the JSON-encoded payload to RX. Splits into chunks if
        larger than CHUNK_DATA_BUDGET; otherwise sends as a single chunk
        with marker=0x02. Silently no-ops if not connected."""
        client = self._client
        if client is None or not client.is_connected:
            log.debug("skip write: not connected")
            return

        body = json.dumps(payload).encode("utf-8")
        total_len = len(body)
        budget = self.CHUNK_DATA_BUDGET
        # Split body into chunks of `budget` bytes each.
        chunks: list[bytes] = []
        for offset in range(0, total_len, budget):
            chunks.append(body[offset:offset + budget])
        if not chunks:
            chunks = [b""]
        last = len(chunks) - 1
        try:
            for i, chunk in enumerate(chunks):
                if last == 0:
                    marker = 0x02  # single chunk -> last
                elif i == 0:
                    marker = 0x00  # first
                elif i == last:
                    marker = 0x02  # last
                else:
                    marker = 0x01  # middle
                framed = bytes([marker]) + chunk
                await client.write_gatt_char(RX_CHAR_UUID, framed, response=True)
            log.info("wrote %d bytes in %d chunk(s): claude.ok=%s claude.s=%s focus.agent=%s",
                     total_len, len(chunks),
                     payload.get("claude", {}).get("ok"),
                     payload.get("claude", {}).get("s"),
                     payload.get("focus", {}).get("agent"))
        except Exception as e:
            log.warning("chunked write failed at chunk %d/%d (%d total bytes): %s",
                        i + 1 if 'i' in dir() else 0, len(chunks), total_len, e)
            await self._disconnect()

    def _handle_req_notify(self, sender, data: bytes) -> None:
        """Called by bleak when the device writes to REQ characteristic.
        Fires `on_refresh` callback so the orchestrator can trigger pollers."""
        if self.on_refresh is not None:
            self.on_refresh()

    async def _disconnect(self) -> None:
        if self._client is not None:
            try:
                await self._client.disconnect()
            except Exception:
                pass
            self._client = None

    async def _scan_and_connect(self) -> bool:
        """Returns True if connected, False if scan timed out / no device."""
        log.info("scanning for %r...", DEVICE_NAME)
        # macOS CoreBluetooth surfaces the device name via either d.name (when
        # paired/cached) or adv.local_name (fresh advertisement). Check both.
        def _matches(d, adv) -> bool:
            return d.name == DEVICE_NAME or (adv and adv.local_name == DEVICE_NAME)
        device = await BleakScanner.find_device_by_filter(_matches, timeout=15)
        if device is None:
            return False

        log.info("connecting to %s", device.address)
        client = BleakClient(device)
        await client.connect()
        await client.start_notify(REQ_CHAR_UUID, self._handle_req_notify)
        self._client = client
        log.info("connected")
        # Notify the orchestrator so it can flush the current state to the
        # freshly-connected peer (writes attempted before connect were silently
        # skipped).
        if self.on_connected is not None:
            self.on_connected()
        return True

    async def run(self) -> None:
        """Long-running task. Maintains connection; reconnects on drop."""
        backoff = 1
        while not self._stopped.is_set():
            try:
                if self._client is None or not self._client.is_connected:
                    ok = await self._scan_and_connect()
                    if not ok:
                        await asyncio.sleep(backoff)
                        backoff = min(60, backoff * 2)
                        continue
                    backoff = 1
                # Heartbeat — sleep a bit, then check still connected
                await asyncio.sleep(5)
            except Exception as e:
                log.warning("ble loop error: %s", e)
                await self._disconnect()
                await asyncio.sleep(backoff)
                backoff = min(60, backoff * 2)

    def stop(self) -> None:
        self._stopped.set()
