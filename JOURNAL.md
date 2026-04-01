# CEF Agent-Browser Project Journal
**Last updated:** 2026-03-31
**Branch:** `zero-user-native` on `github.com/maceip/cef` (canonical source for this journal; see addendum for `bench/site` on `cursor/journal-action-items-9cc6`)
**Build machine:** `ec2-3-120-153-36.eu-central-1.compute.amazonaws.com` (32-core AMD EPYC 7571, 123GB RAM, 1.2TB NVMe)

---

## Current State

### Release Build: SUCCEEDED ✓
- **Date:** 2026-03-31 13:23 UTC
- **Location:** `/home/ubuntu/cef-build/chromium/src/out/Release_GN_x64/`
- **Binaries:** `libcef.so` (618MB), `cefclient` (2.8MB), `cefsimple` (1.5MB)
- **Chromium:** 147.0.7727.0
- **GN flags:** proprietary_codecs=true, ffmpeg_branding="Chrome", enable_nacl=false, blink_symbol_level=0, v8_symbol_level=0, symbol_level=0, enable_remoting=false, use_vaapi=false, rtc_use_pipewire=false

### What's Built and Committed (3 commits + journal, pushed to GitHub)
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

## Remaining Issues

### 1. CEF Headless CDP Message Pump (BLOCKS BENCHMARKS)
When CEF runs headlessly (with Xvfb), the DevTools CDP WebSocket server stops responding after a few seconds. The UI thread message pump starves because X11 events are empty.

**Untried fixes:**
1. `--enable-features=UseOzonePlatform --ozone-platform=headless` — Chromium's headless Ozone backend
2. `CefSettings.multi_threaded_message_loop = true` — background thread pump
3. Custom minimal app with `CefDoMessageLoopWork()` timer
4. Chrome's `--headless=new` flag

### 2. Annotated Screenshot Pipeline
Full Skia pipeline built locally but stripped from remote build (internal `content/browser/accessibility/` headers not accessible from `libcef_static`). Needs CDP-based AX tree walk.

### 3. CEF Translator + Inner Enums
`CefAutomationProgram::InstructionType` breaks `translator.py`. Move to top-level enum.

### 4. Test Suites Disabled
42 tests written but disabled (need `libcef_static` linkage).

### 5. Build Fixes Not in Git
All the API compat fixes exist only on the remote's copy of CEF. Need to be committed.

### 6. Stagehand Benchmark
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

## Next Steps (Prioritized)

### P0 — Get Our Numbers
1. ~~Fix mojo disconnect cascade~~ ✅
2. ~~Get the build to succeed~~ ✅
3. ~~Add proprietary codecs + PDF~~ ✅
4. **Fix headless CDP** — try `--ozone-platform=headless` on EPYC machine
5. **Run our benchmark** — 6 real agentic tasks via CDP against cefsimple
6. **Run Stagehand + browser-use benchmarks** on EPYC machine

### P1 — Ship
7. **Commit build fixes** to git (diff from remote CEF copy)
8. **Write the blog post** with benchmark results + architecture overview
9. **Build surfcomp website** — COSS style, Pachinko viz, Cladogram viz

### P2 — Harden
10. **Fix CEF translator** — move InstructionType to top-level
11. **Generate CAPI headers** → enable cef-rs Rust bindings
12. **Re-enable test suites** — separate GN target or public API only
13. **Implement annotated screenshot via CDP** — replace internal headers
14. **WebAuthn/passkey support** — wire credential storage

---

## Addendum — `bench/site` (TypeScript) on `cursor/journal-action-items-9cc6`

This branch merges `zero-user-native` with **`master`**-derived work: trajectory distillation types/helpers under `bench/site/`, CAPI header work from PR #7, etc.

### Completed
- Trajectory distillation uses only **trusted** provenance for aggregates; external scores stay in `excludedExternalScores`.
- `overall` driver summary matches filtered task rows (known `taskId` only).
- Unit tests: `bench/site/src/data/stats.test.ts` (`npm test` in `bench/site`).
- Docs: `bench/site/README.md`.

### Open (bench site)
- Load real accuracy JSON into the data layer (replace or augment `MOCK_REPORT`).
- Minimal HTML (or framework) view: speed + accuracy + distillation; label external scores **untrusted**.
- CI: `cd bench/site && npm ci && npm run typecheck && npm test` when `bench/site/` changes.
- More tests: no trusted drivers, all unknown task IDs, mixed provenance in one file.
