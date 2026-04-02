# CEF Agent-Browser Project Journal
**Last updated:** 2026-04-02
**Branch:** `master` on `github.com/maceip/cef`
**Build machine:** `ec2-3-120-153-36.eu-central-1.compute.amazonaws.com` (32-core AMD EPYC 7571, 123GB RAM, 1.2TB NVMe, Ubuntu 24.04)

---

## Current State

### Build Status
- **libcef.so (619MB):** SUCCEEDED ✓
- **cefsimple:** BLOCKED — `libcef_dll_wrapper` fails to compile (missing CAPI translation layer for custom API types)
- **Chromium:** 147.0.7727.0
- **Location on EPYC:** `/home/ubuntu/cef-build/chromium/src/out/Release_GN_x64/`

### What's Built and Committed
- **19 performance features** from the `.landmines` catalog — all implemented and wired
- **6 benchmark-informed features** from Browser Use Mind2Web analysis
- **Element reference system** (`@e1`/`@e2` style refs)
- **Benchmark harness** — 6 real agentic tasks × 4 frameworks
- **Build scripts** — `tools/claude/build.sh`, `test-perf.sh`, `apply-patches.sh`
- **6 test suites** (42 tests) — written but disabled from ceftests

### Benchmark Results (3 of 5 frameworks)
| Framework | Wall Time | Tasks | Notes |
|-----------|-----------|-------|-------|
| CDP Raw | **4.2s** | 6/6, ~95% pass | Raw DevTools Protocol baseline |
| agent-browser | **14.9s** | 6/6, ~70% pass | CLI overhead ~150ms/cmd |
| Playwright | **127s** | 6/6, ~80% pass | 90s of timeouts on bot-blocked sites |
| browser-use | ran | 6/6 | Standalone run completed |
| Stagehand | crashed | 0/6 | CHROME_PATH + init config issues (fixable) |
| **CEF (us)** | **NOT YET** | — | Headless CDP message pump issue |

---

## Resolved Issues

### ✅ Mojo Disconnect Handler Cascade
**Solution:** Keep `OnDisconnect` with 4 args (including `MojoResult error_result`), keep `GetDisconnectDebugString` with 7 args. The 2-arg mojo `set_disconnect_with_reason_handler` callbacks pass `MOJO_RESULT_OK` as the 4th arg to `OnDisconnect`. This preserves all internal logging and retry logic without modifying the function chain.

### ✅ Build Compilation (~15 API Compat Fixes)
All fixed on the remote. The complete list:
- `base::Value::Dict` → `base::DictValue` (9 files)
- `base::Value::List` → `base::ListValue` (2 files)
- `base::JSONReader::ReadDict(json)` → `ReadDict(json, base::JSON_PARSE_RFC)` (4 files)
- `FrameTreeNodeId` → `.value()` only for `BumpGeneration()` calls
- `FileEnumerator::FileInfo` uses `GetSize()`/`GetLastModifiedTime()` (not `.size`/`.last_modified`)
- `base::File::Info` uses `.size`/`.last_modified` (not methods)
- `string_view` → explicit `std::string()` construction (3 sites)
- `base::Environment::GetVar` → returns `optional<string>` (new API)
- `crypto::Aead::AuthTagLength()` → hardcoded 16 (AES-256-GCM)
- `crypto::Aead::Init(key)` → `Init(base::span<const uint8_t>(key))`
- Nested struct default initializers removed (C++ standard issue with enclosing class)
- `EvalResult.metadata` → `std::string metadata_json` (base::DictValue is move-only)
- `CefAuthProfileTraits` added to `cef_types_wrappers.h` (proper CefStructBase)
- `IMPLEMENT_REFCOUNTING_DELETE_ON_UIT` → `IMPLEMENT_REFCOUNTING`
- `ExecuteJavaScriptWithResult` stub added to renderer `frame_impl.h`
- `DevToolsAgentAttached/Detached` implementations inside `namespace content`
- `gpu/webgpu/DAWN_VERSION` created with git hash
- `gpu/webgpu/dawn_commit_hash.h` created
- `cef_api_untracked.json` with version 999999
- Disabled `mojo_connect_result_3664.patch`
- Disabled npm Skia registry, enabled optional deps for rollup
- Re-applied `content_2015.patch` for renderer + `chrome_browser_context_menus.patch`

### ✅ Proprietary Codecs + PDF
Added to GN args: `proprietary_codecs=true`, `ffmpeg_branding="Chrome"`. Build succeeds with H264/AAC/MP3/MP4 support.

---

## Blocking Issue: Missing CAPI Translation Layer

### Root Cause
The CEF fork added custom C++ APIs (in `include/*.h`) but `translator.py` generation got interrupted and never completed after later API changes. The `libcef_dll_wrapper` build fails because ctocpp wrappers reference types that don't exist as C structs or have stale signatures.

### Confirmed Missing CAPI/Wrappers
These types have no generated C API structs or cpptoc/ctocpp wrappers:
- `CefJavaScriptResultCallback`
- `CefEvalSnapshotCallback`
- `CefCompoundOperationCallback`
- `CefBatchQueryCallback`
- `CefAutomationProgram`
- `CefAutomationProgramCallback`

### Stale Generated Signatures
These wrappers exist but still use `CefStringVisitor` where headers now use newer callback types:
- `CefFrame::ExecuteJavaScriptWithResult` — should use `CefJavaScriptResultCallback`
- `CefBrowserCapture::EvalThenSnapshot` — should use `CefEvalSnapshotCallback`

### Already Present and Correct
These were generated before the interruption and are fine:
- `CefBrowserCapture` CAPI + wrappers
- `CefBrowserSecurityPolicy` CAPI + wrappers
- `CefScreenshotCallback`, `CefBrowserSecurityCallback`
- Snapshot/screenshot settings structs and wrapper traits

### Why translator.py Fails
`translator.py` currently fails because of a nested enum in `include/cef_automation_program.h`:
```cpp
CefAutomationProgram::InstructionType  // nested enum — breaks the CEF parser
```
There is already a file-scope C enum in `include/internal/cef_types.h`, but it doesn't match the public automation-program enum. A dry-run with the enum moved to file scope confirmed the parser succeeds.

---

## Marching Order (P0 — Get cefsimple linking)

**Approach: Fix `translator.py` compatibility, regenerate the CAPI layer properly, build, verify headless CDP, benchmark.**

### Phase 1: Fix translator compatibility
Move `CefAutomationProgram::InstructionType` nested enum to file-scope so the CEF parser can process it. Files to update:
- `include/cef_automation_program.h` — replace nested enum with reference to file-scope enum
- `include/internal/cef_types.h` — update file-scope C enum to match
- `libcef/browser/automation_program_impl.h` — update references
- `libcef/browser/automation_program_impl.cc` — update references

### Phase 2: Run translator.py
```bash
cd /home/ubuntu/cef-build/chromium/src/cef
python3 tools/translator.py --root-dir ..
python3 tools/version_manager.py
```
This should auto-generate/fix:
- Missing callback/program CAPI structs and wrappers (all 6 types above)
- Stale frame/browser-capture signatures (`CefStringVisitor` → correct callback types)
- `cef_paths.gypi` and wrapper registration glue

### Phase 3: Inspect regenerated outputs
Confirm:
- `ExecuteJavaScriptWithResult` uses `CefJavaScriptResultCallback` (not `CefStringVisitor`)
- `EvalThenSnapshot` uses `CefEvalSnapshotCallback`
- All 6 missing CAPI structs/files now exist
- `cef_paths.gypi` includes the new wrapper files

### Phase 4: Build cefsimple
```bash
cd /home/ubuntu/cef-build/chromium/src/cef
git pull origin master

cd /home/ubuntu/cef-build/chromium/src
buildtools/linux64/gn gen out/Release_GN_x64
ninja -C out/Release_GN_x64 cefsimple
```

### Phase 5: Verify headless CDP
```bash
cd /home/ubuntu/cef-build/chromium/src/out/Release_GN_x64
./cefsimple --external-message-pump --use-ozone-headless --remote-debugging-port=9222 &
sleep 3
curl http://127.0.0.1:9222/json    # Should return tab list JSON
```

### Phase 6: Run benchmarks
Run 6-task benchmark suite against cefsimple's CDP endpoint, compare against existing baselines:
- CDP Raw: 4.2s
- agent-browser: 14.9s
- Playwright: 127s

---

## Other Remaining Issues

### Annotated Screenshot Pipeline
Full Skia pipeline built locally but stripped from remote build (internal `content/browser/accessibility/` headers not accessible from `libcef_static`). Needs CDP-based AX tree walk.

### Test Suites Disabled
42 tests written but disabled (need `libcef_static` linkage).

### Stagehand Benchmark
Crashed on init. Needs `CHROME_PATH` set + local npm install (both done on remote).

---

## Architecture Notes

### Proud Of
1. **Performance catalog methodology** — `.landmines` → fingerprinting review → agent-safety → scrapping 3 items → implementing 13
2. **Policy enforcement pipeline** — `ShouldBlockRequest()` with pre-compiled domain matchers in `OnBeforeRequest()`, returns `net::ERR_BLOCKED_BY_CLIENT`
3. **Element reference system** — `@e1`/`e1`/`1` parsing, `BuildRefText()`, thread-safe, generation-tracked
4. **Stealth config** — 9 toggleable anti-detection JS patches, `Object.defineProperty` with `configurable: true`
5. **Action trace** — 3-level progressive disclosure (summary → detail → full with snapshots)
6. **Mojo fix approach** — lambda wrapper preserving 4-arg internal API while using 2-arg upstream callbacks

### Concerning
1. **Build fragility** — 15+ manual fixes per fresh checkout. Need a single compat patch.
2. **No CEF benchmark numbers** — headless CDP is the blocker
3. **`cef_api_untracked.json` hack** — fake version 999999 with borrowed hash
4. **Annotated screenshot stripped** — the production Skia pipeline exists locally but can't compile on the build machine

---

## Remote Machine State

```
/home/ubuntu/cef-build/
├── chromium/src/                           # Chromium 147.0.7727.0
│   ├── cef/                                # Our fork (with build fixes applied)
│   └── out/Release_GN_x64/                 # RELEASE BUILD ✓
│       ├── libcef.so                       # 618MB
│       ├── cefclient                       # 2.8MB
│       └── cefsimple                       # 1.5MB
├── cef/                                    # Clean clone of zero-user-native
└── depot_tools/                            # Fresh clone
```

**Installed frameworks:** agent-browser 0.22.3, browser-use 0.12.5, Playwright 1.58.0, Stagehand 3.2.0 (local), Chrome DevTools MCP 0.20.3

---

## Build Instructions

### Prerequisites
- Ubuntu 24.04 (tested on AMD EPYC 7571, 32 cores, 123GB RAM)
- ~200GB disk for Chromium checkout + build
- `depot_tools` in PATH

### Initial Setup (one-time)
```bash
# Clone depot_tools
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH="$PWD/depot_tools:$PATH"

# Fetch Chromium 147
mkdir cef-build && cd cef-build
fetch --nohooks chromium
cd chromium/src
git checkout 147.0.7727.0
gclient sync --with_branch_heads --jobs 16

# Clone our CEF fork into chromium/src/cef
git clone https://github.com/maceip/cef.git cef
cd cef
git checkout master

# Create required stamp files (normally generated by translator.py)
touch VERSION.stamp
# cef_api_untracked.json is committed in the repo

# Configure the build
cd ..
cat > out/Release_GN_x64/args.gn << 'EOF'
blink_heap_inside_shared_library=true
clang_use_chrome_plugins=false
enable_background_mode=false
enable_downgrade_processing=false
enable_linux_installer=false
enable_resource_allowlist_generation=false
enable_widevine=true
enable_remoting=false
is_component_build=false
is_debug=false
optimize_webui=true
target_cpu="x64"
use_qt5=false
use_qt6=false
use_sysroot=false
rtc_use_pipewire=false
use_vaapi=false
treat_warnings_as_errors=false
symbol_level=0
proprietary_codecs=true
ffmpeg_branding="Chrome"
enable_pdf=true
blink_symbol_level=0
v8_symbol_level=0
enable_nacl=false
EOF

buildtools/linux64/gn gen out/Release_GN_x64
```

### Building
```bash
cd /home/ubuntu/cef-build/chromium/src

# Build libcef.so + cefsimple (currently cefsimple is BLOCKED, see Marching Order above)
ninja -C out/Release_GN_x64 cefsimple

# Build just libcef.so (this succeeds)
ninja -C out/Release_GN_x64 libcef
```

### Updating after git pull
```bash
cd /home/ubuntu/cef-build/chromium/src/cef
git pull origin master
cd ..
buildtools/linux64/gn gen out/Release_GN_x64
ninja -C out/Release_GN_x64 cefsimple
```

---

## Next Steps (Prioritized)

### P0 — Get cefsimple Linking (see Marching Order above)
1. ~~Fix mojo disconnect cascade~~ ✅
2. ~~Build libcef.so~~ ✅
3. ~~Fix cefsimple linker errors (shared sources, views ctocpp, AppendSwitchASCII)~~ ✅
4. **Fix translator.py compatibility** — move nested enum to file scope (Phase 1)
5. **Run translator.py** — regenerate all missing CAPI wrappers (Phase 2)
6. **Build cefsimple** (Phase 4)
7. **Verify headless CDP** — `--external-message-pump --use-ozone-headless --remote-debugging-port=9222` (Phase 5)
8. **Run benchmarks** — 6 real agentic tasks via CDP (Phase 6)

### P1 — Ship
9. **Write blog post** with benchmark results + architecture overview
10. **Build surfcomp website** — COSS style, Pachinko viz, Cladogram viz

### P2 — Harden
11. **Enable cef-rs bindings** to call custom APIs
12. **Re-enable test suites** — separate GN target or public API only
13. **Implement annotated screenshot via CDP** — replace internal headers
