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

    async def write_payload(self, payload: dict) -> None:
        """Write the JSON-encoded payload to the RX characteristic.
        Silently no-ops if not connected — the orchestrator's connection loop
        will reconnect and the next write will go through."""
        client = self._client
        if client is None or not client.is_connected:
            log.debug("skip write: not connected")
            return
        data = json.dumps(payload).encode("utf-8")
        try:
            await client.write_gatt_char(RX_CHAR_UUID, data, response=False)
            log.info("wrote %d bytes: claude.ok=%s claude.s=%s focus.agent=%s",
                     len(data),
                     payload.get("claude", {}).get("ok"),
                     payload.get("claude", {}).get("s"),
                     payload.get("focus", {}).get("agent"))
        except Exception as e:
            log.warning("write failed (%d bytes): %s", len(data), e)
            # Force a reconnect on next loop iteration
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
