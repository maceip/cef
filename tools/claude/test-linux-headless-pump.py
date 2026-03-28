#!/usr/bin/env python3
# Copyright (c) 2026 The Chromium Embedded Framework Authors.
# All rights reserved.

import asyncio
import json
import os
import signal
import subprocess
import sys
import time
import urllib.request


DISPLAY = ":99"
DEBUG_PORT = 9333


async def wait_for_json_endpoint(port: int, timeout_s: float) -> list[dict]:
    deadline = time.time() + timeout_s
    last_error = None
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/json",
                                        timeout=1.0) as response:
                return json.loads(response.read())
        except Exception as exc:  # pragma: no cover - exercised in integration
            last_error = exc
            await asyncio.sleep(0.2)
    raise RuntimeError(f"Timed out waiting for CDP endpoint: {last_error}")


async def send_cdp(ws, msg_id: int, method: str, params: dict | None = None):
    payload = {"id": msg_id, "method": method}
    if params:
        payload["params"] = params
    await ws.send(json.dumps(payload))
    while True:
        raw = await ws.recv()
        msg = json.loads(raw)
        if msg.get("id") == msg_id:
            if "error" in msg:
                raise RuntimeError(f"CDP error for {method}: {msg['error']}")
            return msg.get("result", {})


async def run_probe(binary: str, runtime_s: float):
    try:
        import websockets
    except ImportError as exc:  # pragma: no cover - environment dependent
        raise RuntimeError(
            "python websockets package is required for CDP longevity test") from exc

    xvfb = subprocess.Popen(
        ["Xvfb", DISPLAY, "-screen", "0", "1280x800x24"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    env = dict(os.environ)
    env["DISPLAY"] = DISPLAY

    browser = subprocess.Popen(
        [
            binary,
            "--no-sandbox",
            f"--remote-debugging-port={DEBUG_PORT}",
            "--external-message-pump",
            "--url=data:text/html,<html><body><button id='go'>go</button></body></html>",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        env=env,
        start_new_session=True,
    )

    try:
        targets = await wait_for_json_endpoint(DEBUG_PORT, timeout_s=20.0)
        if not targets:
            raise RuntimeError("No DevTools targets reported")
        ws_url = targets[0]["webSocketDebuggerUrl"]

        async with websockets.connect(ws_url, max_size=16 * 1024 * 1024) as ws:
            next_id = 1
            await send_cdp(ws, next_id, "Page.enable")
            next_id += 1
            await send_cdp(ws, next_id, "Runtime.enable")
            next_id += 1
            await send_cdp(ws, next_id, "Accessibility.enable")
            next_id += 1

            deadline = time.time() + runtime_s
            count = 0
            while time.time() < deadline:
                result = await asyncio.wait_for(
                    send_cdp(
                        ws,
                        next_id,
                        "Runtime.evaluate",
                        {
                            "expression": f"'tick-{count}'",
                            "returnByValue": True,
                        },
                    ),
                    timeout=5.0,
                )
                next_id += 1
                value = result.get("result", {}).get("value")
                expected = f"tick-{count}"
                if value != expected:
                    raise RuntimeError(
                        f"Unexpected CDP value: got={value!r} expected={expected!r}")
                count += 1
                await asyncio.sleep(1.0)

            print(f"CDP longevity OK: {count} successful Runtime.evaluate calls")
    finally:
        for proc in (browser, xvfb):
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        for proc in (browser, xvfb):
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: test-linux-headless-pump.py <cef-binary> [runtime-seconds]",
              file=sys.stderr)
        return 2

    binary = sys.argv[1]
    runtime_s = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
    asyncio.run(run_probe(binary, runtime_s))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
