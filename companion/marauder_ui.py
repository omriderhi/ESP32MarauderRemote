#!/usr/bin/env python3
"""
ESP32 Marauder Companion — USB serial bridge with browser UI.

Usage:
    python marauder_ui.py [--port PORT] [--baud BAUD] [--host HOST] [--ws-port PORT]

Serves http://localhost:2337 and opens it in the default browser.
"""

import argparse
import asyncio
import http.server
import json
import logging
import os
import socket
import threading
import time
import webbrowser
from pathlib import Path

import serial
import serial.tools.list_ports
import websockets

# ── configuration ────────────────────────────────────────────────────────────

DEFAULT_BAUD = 115200
DEFAULT_HOST = "127.0.0.1"
DEFAULT_WS_PORT = 2337

KNOWN_VIDS = {
    "1a86",  # CH340 / CH341
    "10c4",  # CP2102 / CP2104 (Silicon Labs)
    "0403",  # FTDI FT232
    "303a",  # Espressif native USB (ESP32-S2/S3)
}

DESCRIPTION_KEYWORDS = [
    "ch340", "ch341", "cp210", "ftdi", "uart", "usb serial",
    "usb-serial", "esp32", "marauder",
]

INDEX_HTML = Path(__file__).with_name("index.html")

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("marauder")

# ── serial auto-detect ────────────────────────────────────────────────────────

def find_marauder_port() -> str | None:
    for info in serial.tools.list_ports.comports():
        vid = f"{info.vid:04x}" if info.vid else ""
        desc = (info.description or "").lower()
        if vid in KNOWN_VIDS or any(kw in desc for kw in DESCRIPTION_KEYWORDS):
            log.info("Auto-detected %s — %s", info.device, info.description)
            return info.device
    return None

# ── bridge state ─────────────────────────────────────────────────────────────

class Bridge:
    def __init__(self, port: str | None, baud: int):
        self.port = port
        self.baud = baud
        self.ser: serial.Serial | None = None
        self.clients: set[websockets.WebSocketServerProtocol] = set()
        self.connected = False
        self._lock = asyncio.Lock()

    # ── WebSocket handlers ────────────────────────────────────────────────────

    async def ws_handler(self, ws: websockets.WebSocketServerProtocol):
        self.clients.add(ws)
        status = {
            "type": "status",
            "connected": self.connected,
            "port": self.port or "",
            "baud": self.baud,
        }
        await ws.send(json.dumps(status))
        try:
            async for msg in ws:
                if isinstance(msg, str):
                    cmd = msg.strip()
                    if cmd:
                        await self._serial_write(cmd + "\n")
        except websockets.ConnectionClosed:
            pass
        finally:
            self.clients.discard(ws)

    async def _broadcast(self, obj: dict):
        if not self.clients:
            return
        data = json.dumps(obj)
        await asyncio.gather(
            *(c.send(data) for c in list(self.clients)),
            return_exceptions=True,
        )

    async def _serial_write(self, text: str):
        if self.ser and self.ser.is_open:
            try:
                self.ser.write(text.encode("utf-8", errors="replace"))
            except serial.SerialException as e:
                log.warning("Write failed: %s", e)

    # ── serial read loop ──────────────────────────────────────────────────────

    async def serial_loop(self):
        loop = asyncio.get_event_loop()
        while True:
            port = self.port or find_marauder_port()
            if not port:
                if self.connected:
                    self.connected = False
                    self.port = None
                    await self._broadcast({"type": "status", "connected": False, "port": "", "baud": self.baud})
                await asyncio.sleep(1)
                continue

            try:
                ser = serial.Serial(port, self.baud, timeout=0.1)
                self.ser = ser
                self.port = port
                self.connected = True
                await self._broadcast({"type": "status", "connected": True, "port": port, "baud": self.baud})
                log.info("Connected to %s at %d baud", port, self.baud)

                buf = b""
                while True:
                    chunk = await loop.run_in_executor(None, ser.read, 256)
                    if chunk:
                        buf += chunk
                        while b"\n" in buf:
                            line, buf = buf.split(b"\n", 1)
                            text = line.decode("utf-8", errors="replace").rstrip("\r")
                            await self._broadcast({"type": "output", "text": text})
                    elif not ser.is_open:
                        break

            except serial.SerialException as e:
                log.warning("Serial error (%s): %s — retrying in 1 s", port, e)
                self.connected = False
                self.ser = None
                if self.port == port:
                    self.port = None
                await self._broadcast({"type": "status", "connected": False, "port": "", "baud": self.baud})
                await asyncio.sleep(1)

# ── HTTP server (serves index.html) ──────────────────────────────────────────

class _Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(INDEX_HTML.parent), **kwargs)

    def log_message(self, fmt, *args):
        pass  # silence access log

def _run_http(host: str, port: int):
    server = http.server.HTTPServer((host, port), _Handler)
    server.serve_forever()

# ── entry point ───────────────────────────────────────────────────────────────

async def _main(args):
    bridge = Bridge(args.port, args.baud)

    http_thread = threading.Thread(
        target=_run_http, args=(args.host, args.ws_port), daemon=True
    )
    http_thread.start()

    url = f"http://{args.host}:{args.ws_port}"
    log.info("UI available at %s", url)

    # slight delay so the HTTP server is ready before the browser opens
    await asyncio.sleep(0.4)
    webbrowser.open(url)

    async with websockets.serve(bridge.ws_handler, args.host, args.ws_port + 1):
        log.info("WebSocket listening on ws://%s:%d", args.host, args.ws_port + 1)
        await bridge.serial_loop()


def main():
    parser = argparse.ArgumentParser(description="ESP32 Marauder Companion")
    parser.add_argument("--port", default=None, help="Serial port (auto-detected if omitted)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--ws-port", type=int, default=DEFAULT_WS_PORT, dest="ws_port")
    args = parser.parse_args()
    asyncio.run(_main(args))


if __name__ == "__main__":
    main()
