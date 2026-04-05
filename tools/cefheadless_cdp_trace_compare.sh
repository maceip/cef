#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <path-to-cefheadless-binary> [base-port]"
  exit 1
fi

CEFHEADLESS_BIN="$1"
BASE_PORT="${2:-9222}"

if [[ ! -x "$CEFHEADLESS_BIN" ]]; then
  echo "error: binary not executable: $CEFHEADLESS_BIN"
  exit 1
fi

ARTIFACT_DIR="/tmp/cefheadless-cdptrace-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$ARTIFACT_DIR"

run_mode() {
  local mode="$1"
  local port="$2"
  shift 2
  local extra_flags=("$@")

  local run_log="$ARTIFACT_DIR/${mode}.log"
  local version_json="$ARTIFACT_DIR/${mode}.json.version"
  local list_json="$ARTIFACT_DIR/${mode}.json.list"

  echo "==> running mode=$mode port=$port"
  "$CEFHEADLESS_BIN" \
    --url=about:blank \
    --remote-debugging-address=127.0.0.1 \
    --remote-debugging-port="$port" \
    "${extra_flags[@]}" \
    >"$run_log" 2>&1 &
  local pid=$!

  local version_ok=0
  local list_ok=0
  for _ in $(seq 1 50); do
    if curl -fsS "http://127.0.0.1:${port}/json/version" >"$version_json" 2>/dev/null; then
      version_ok=1
    fi
    if curl -fsS "http://127.0.0.1:${port}/json/list" >"$list_json" 2>/dev/null; then
      list_ok=1
    fi
    if [[ $version_ok -eq 1 && $list_ok -eq 1 ]]; then
      break
    fi
    sleep 0.2
  done

  echo "  /json/version success: $version_ok"
  echo "  /json/list    success: $list_ok"

  kill "$pid" >/dev/null 2>&1 || true
  wait "$pid" >/dev/null 2>&1 || true
}

run_mode "good" "$BASE_PORT"
run_mode "bad_no_user_data_dir" "$((BASE_PORT + 1))" --cdp-bad-no-user-data-dir
run_mode "bad_windowless" "$((BASE_PORT + 2))" --cdp-bad-windowless

echo
echo "Artifacts: $ARTIFACT_DIR"
echo "Marker counts:"
for log in "$ARTIFACT_DIR"/*.log; do
  echo "---- $(basename "$log") ----"
  grep -E "CDPTRACE_(HTTP_ENTRY|JSON_VERSION_REQUEST_ENTRY|TARGET_DISCOVERY_START|HTTP_IO_TO_UI_POST|HTTP_IO_TO_UI_RUN|READY_|AGENT_HOST_|TARGET_CLASSIFIED|HEADLESS_)" "$log" | wc -l
done

echo
echo "Sample diff command:"
echo "  diff -u \"$ARTIFACT_DIR/good.log\" \"$ARTIFACT_DIR/bad_no_user_data_dir.log\" | less"
