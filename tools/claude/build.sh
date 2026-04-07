#!/bin/bash
# CEF Local Build Pipeline — run ./tools/claude/build.sh help for usage.
set -euo pipefail

CEF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHROMIUM_DIR="${CHROMIUM_DIR:-$(dirname "$CEF_DIR")}"
DEPOT_TOOLS="${DEPOT_TOOLS:-$HOME/depot_tools}"
BUILD_DIR_DEBUG="${CHROMIUM_DIR}/out/Debug_GN_x64"
BUILD_DIR_RELEASE="${CHROMIUM_DIR}/out/Release_GN_x64"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 8)}"

log() { echo "==> $*"; }
err() { echo "ERROR: $*" >&2; exit 1; }

ensure_depot_tools() {
  if [ ! -d "$DEPOT_TOOLS" ]; then
    log "Fetching depot_tools..."
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS"
  fi
  export PATH="$DEPOT_TOOLS:$PATH"
}

cmd_setup() {
  log "Setting up CEF build environment"
  ensure_depot_tools
  mkdir -p "$BUILD_DIR_DEBUG"

  # Symlink CEF into Chromium if needed
  if [ ! -d "$CHROMIUM_DIR/cef" ] && [ ! -L "$CHROMIUM_DIR/cef" ] \
     && [ "$CHROMIUM_DIR" != "$(dirname "$CEF_DIR")" ]; then
    log "Symlinking $CEF_DIR -> $CHROMIUM_DIR/cef"
    ln -sf "$CEF_DIR" "$CHROMIUM_DIR/cef"
  fi

  cat > "$BUILD_DIR_DEBUG/args.gn" << 'ARGS'
is_debug = true
is_component_build = true
symbol_level = 1
dcheck_always_on = true
use_sysroot = true
use_allocator = "none"
treat_warnings_as_errors = false
ARGS

  command -v gn &>/dev/null && gn gen "$BUILD_DIR_DEBUG" \
    || err "gn not found. Add depot_tools to PATH: export PATH=\$HOME/depot_tools:\$PATH"
}

cmd_build() {
  ensure_depot_tools
  log "Building CEF (debug)..."

  if [ ! -f "$BUILD_DIR_DEBUG/args.gn" ]; then
    err "Build directory not set up. Run: ./tools/claude/build.sh setup"
  fi

  autoninja -C "$BUILD_DIR_DEBUG" cef cefclient cefsimple ceftests -j "$JOBS"
  log "Build complete."
}

cmd_release() {
  ensure_depot_tools
  log "Building CEF (release)..."

  mkdir -p "$BUILD_DIR_RELEASE"
  if [ ! -f "$BUILD_DIR_RELEASE/args.gn" ]; then
    cat > "$BUILD_DIR_RELEASE/args.gn" << 'ARGS'
is_debug = false
is_component_build = false
symbol_level = 0
is_official_build = true
use_sysroot = true
treat_warnings_as_errors = false
ARGS
    gn gen "$BUILD_DIR_RELEASE"
  fi

  autoninja -C "$BUILD_DIR_RELEASE" cef cefclient cefsimple -j "$JOBS"
  log "Release build complete."
}

cmd_test() {
  log "Running ceftests..."

  local test_binary="$BUILD_DIR_DEBUG/ceftests"
  if [ ! -f "$test_binary" ]; then
    err "ceftests binary not found. Run: ./tools/claude/build.sh build"
  fi

  # Run with gtest filter if provided
  local filter="${1:-*}"
  "$test_binary" --gtest_filter="$filter" --no-sandbox 2>&1 | tee /tmp/ceftests.log

  log "Test results saved to /tmp/ceftests.log"
}

cmd_test_perf() {
  local test_binary="$BUILD_DIR_DEBUG/ceftests"
  if [ ! -f "$test_binary" ]; then
    err "ceftests binary not found. Run: ./tools/claude/build.sh build"
  fi

  local suites=("StateJournal" "PageModelCache" "AgentScheduler" "SecurityPolicy" "DirtyTracking" "SessionPool")
  local filter="" verbose=""

  for arg in "$@"; do
    case "$arg" in
      -v|--verbose) verbose="--gtest_print_time=1" ;;
      *) filter="$arg.*" ;;
    esac
  done

  if [ -z "$filter" ]; then
    filter=$(IFS=":"; echo "${suites[*]/%/.*}")
    filter="${filter// /}"
  fi

  log "Running perf tests: $filter"
  "$test_binary" --gtest_filter="$filter" $verbose --no-sandbox 2>&1 | tee /tmp/ceftests-perf.log
}

cmd_quick() {
  ensure_depot_tools
  log "Quick incremental build + test..."

  # Build only ceftests target for fastest iteration
  autoninja -C "$BUILD_DIR_DEBUG" ceftests -j "$JOBS"
  cmd_test_perf
}

cmd_check() {
  ensure_depot_tools
  log "Compilation check (no link)..."

  # Build just the object files for our new sources
  local new_files=(
    "obj/cef/libcef/browser/agent_scheduler.o"
    "obj/cef/libcef/browser/automation_program_impl.o"
    "obj/cef/libcef/browser/page_model_cache.o"
    "obj/cef/libcef/browser/session_pool.o"
    "obj/cef/libcef/browser/state_journal.o"
  )

  autoninja -C "$BUILD_DIR_DEBUG" "${new_files[@]}" -j "$JOBS" 2>&1 || true
  log "Check complete."
}

cmd_all() {
  cmd_build
  cmd_test
}

cmd_help() {
  echo "CEF Local Build Pipeline"
  echo ""
  echo "Usage: ./tools/claude/build.sh <command> [args]"
  echo ""
  echo "Commands:"
  echo "  setup      First-time setup (fetch depot_tools, create build dirs)"
  echo "  build      Debug build (cef + cefclient + cefsimple + ceftests)"
  echo "  release    Release/official build"
  echo "  test [F]   Run ceftests (optional gtest filter F)"
  echo "  test-perf  Run only performance optimization tests"
  echo "  quick      Incremental build + run perf tests (fastest iteration)"
  echo "  check      Compile-only check for new files (no link)"
  echo "  all        Full build + test"
  echo "  help       This message"
  echo ""
  echo "Environment variables:"
  echo "  CHROMIUM_DIR  Path to Chromium source (default: parent of CEF dir)"
  echo "  DEPOT_TOOLS   Path to depot_tools (default: ~/depot_tools)"
  echo "  JOBS          Parallel build jobs (default: nproc)"
}

# Main dispatch
case "${1:-help}" in
  setup)     cmd_setup ;;
  build)     cmd_build ;;
  release)   cmd_release ;;
  test)      cmd_test "${2:-*}" ;;
  test-perf) shift; cmd_test_perf "$@" ;;
  quick)     cmd_quick ;;
  check)     cmd_check ;;
  all)       cmd_all ;;
  help|*)    cmd_help ;;
esac
