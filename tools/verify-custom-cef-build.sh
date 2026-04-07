#!/usr/bin/env bash
# Fast checks that a Release_GN_x64 tree contains custom CEF APIs and can serve CDP.
# Usage: CEF_OUT=/path/to/chromium/src/out/Release_GN_x64 ./tools/verify-custom-cef-build.sh
set -euo pipefail

OUT="${CEF_OUT:-/home/ubuntu/cef-build/chromium/src/out/Release_GN_x64}"
BIN="${CEF_BIN:-$OUT/cefheadless}"
PORT="${CEF_VERIFY_PORT:-9450}"

if [[ ! -f "$OUT/libcef.so" ]]; then
  echo "missing libcef.so under $OUT" >&2
  exit 1
fi
if [[ ! -x "$BIN" ]]; then
  echo "missing executable $BIN" >&2
  exit 1
fi

IMPL="$OUT/obj/cef/libcef_static/automation_program_impl.o"
if [[ -f "$IMPL" ]]; then
  if ! nm "$IMPL" | grep -q 'CefAutomationProgram6Create'; then
    echo "expected CefAutomationProgram::Create (mangled: *CefAutomationProgram6Create*) in $IMPL" >&2
    exit 1
  fi
  echo "ok: automation_program_impl.o contains CefAutomationProgram::Create"
else
  echo "warn: $IMPL not found (skip object symbol check; run full GN build)" >&2
fi

# Note: custom C methods are usually behind cef_*_t vtables, not global exports;
# nm -D libcef.so | grep browser_capture typically matches unrelated Blink symbols.

export LD_LIBRARY_PATH="$OUT"
cleanup() {
  kill "${CEF_PID:-}" 2>/dev/null || true
  wait "${CEF_PID:-}" 2>/dev/null || true
}
trap cleanup EXIT

"$BIN" --no-sandbox \
  --remote-debugging-port="${PORT}" \
  --url=about:blank \
  --user-data-dir="/tmp/cef-verify-$$" &
CEF_PID=$!

for _ in $(seq 1 40); do
  if curl -fsS "http://127.0.0.1:${PORT}/json/version" >/tmp/cef-verify-version.json 2>/dev/null; then
    break
  fi
  sleep 0.25
done

if ! grep -q Protocol-Version /tmp/cef-verify-version.json 2>/dev/null; then
  echo "CDP HTTP endpoint did not come up on port ${PORT}" >&2
  exit 1
fi
echo "ok: CDP HTTP /json/version reachable"

python3 - <<PY
import asyncio, json, sys, urllib.request

port = ${PORT}

async def main():
    r = urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list")
    targets = json.loads(r.read())
    ws_url = targets[0]["webSocketDebuggerUrl"]
    import websockets
    async with websockets.connect(ws_url, max_size=50*1024*1024) as ws:
        mid = 0
        async def send(m, p=None):
            nonlocal mid
            mid += 1
            msg = {"id": mid, "method": m}
            if p:
                msg["params"] = p
            await ws.send(json.dumps(msg))
            while True:
                raw = await ws.recv()
                j = json.loads(raw)
                if j.get("id") == mid:
                    return j
        await send("Page.enable")
        r = await send("Page.captureScreenshot", {"format": "png"})
        if "error" in r:
            print("CDP error:", r["error"], file=sys.stderr)
            sys.exit(1)
        data = r.get("result", {}).get("data", "")
        if len(data) < 100:
            print("screenshot payload too small", file=sys.stderr)
            sys.exit(2)
        print("ok: Page.captureScreenshot", len(data), "base64 chars")

asyncio.run(main())
PY

echo "verify-custom-cef-build: all checks passed"
