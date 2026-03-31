# CEF Agent-Browser Project Journal
**Date:** 2026-03-28
**Branch:** `zero-user-native` on `github.com/maceip/cef`
**Build machine:** `ec2-3-120-153-36.eu-central-1.compute.amazonaws.com` (32-core AMD EPYC, 123GB RAM, 1.2TB NVMe)

---

## Where We Are

### What's Built and Committed (2 commits, pushed)
- **19 performance features** from the `.landmines` catalog — all implemented and wired
- **6 benchmark-informed features** from Browser Use Mind2Web analysis
- **Element reference system** (`@e1`/`@e2` style refs)
- **Benchmark harness** — 6 real agentic tasks × 4 frameworks, Python + TypeScript data layer
- **Build scripts** — `tools/claude/build.sh`, `test-perf.sh`, `apply-patches.sh`
- **6 test suites** (42 tests) — written but disabled from ceftests (need libcef_static linkage)

### What's On the Remote Build Machine (NOT committed)
Chromium 147.0.7727.0 checkout with CEF patches applied. The build is at ~90% compilation but stuck on cascading errors from the mojo disconnect handler removal. All the Chromium core objects (~37,000) compile fine. The failures are in CEF's own renderer code.

---

## Where We're Stuck

### 1. The Mojo Disconnect Handler Cascade (Build Blocker)

**Root cause:** CEF's `mojo_connect_result_3664.patch` added a `set_disconnect_with_reason_and_result_handler` method to mojo that passes 3 args to disconnect callbacks (`uint32_t reason, const std::string& description, MojoResult error_result`). In Chromium 147, the upstream mojo already has `set_disconnect_with_reason_handler` which passes only 2 args (`uint32_t, const std::string&`).

When we revert the CEF patch (because it causes redefinition errors), every call site that uses the 3-arg version breaks. The fix cascade touches:

- `libcef/renderer/frame_impl.h` — handler declarations
- `libcef/renderer/frame_impl.cc` — handler implementations + `OnDisconnect` + `GetDisconnectDebugString`
- The `OnDisconnect` function internally uses `error_result` for logging and connection-retry logic

**What I tried:** Removing the 3rd parameter and adding `const int error_result = 0;` as a local variable. This partially works but there are ~7 call sites where the argument count mismatch cascades through `OnDisconnect` → `GetDisconnectDebugString` → logging functions.

**What the fix should be:** Instead of removing `MojoResult error_result` from everything, keep the `OnDisconnect` function signature with 4 args, and have the 2-arg mojo disconnect callbacks wrap the call with `0` for the 4th arg:
```cpp
receiver_.set_disconnect_with_reason_handler(
    base::BindOnce([](CefFrameImpl* self, uint32_t reason, const std::string& desc) {
        self->OnRenderFrameDisconnect(reason, desc, MOJO_RESULT_OK);
    }, base::Unretained(this)));
```
This preserves all internal logic without needing to change every function in the chain.

### 2. CEF Headless CDP Message Pump (Runtime Blocker)

**The problem:** When CEF runs headlessly (even with Xvfb), the DevTools CDP server stops responding after a few seconds. The UI thread message pump starves because the X11 event stream is empty.

**Why it matters:** We can't benchmark ourselves without headless CDP. Every other framework (agent-browser, playwright, stagehand) runs headless fine.

**Potential fixes (untried):**
1. `--enable-features=UseOzonePlatform --ozone-platform=headless` — uses Chromium's headless Ozone backend instead of X11
2. `CefSettings.multi_threaded_message_loop = true` — moves pump to background thread
3. Write a minimal CEF app that calls `CefDoMessageLoopWork()` in a timer loop
4. Use Chrome's `--headless=new` flag which has its own message pump

### 3. CEF Translator Chokes on Inner Enums

`CefAutomationProgram::InstructionType` is an enum inside a class. The CEF translator (`translator.py`) can't parse it. This blocks CAPI header generation, which blocks cef-rs Rust bindings.

**Fix:** Move `InstructionType` to a top-level enum (`cef_automation_instruction_type_t` in `cef_types.h`), reference it from the class.

---

## What I'm Architecturally Proud Of

### 1. The Performance Catalog Methodology
Starting from `.landmines` → fingerprinting analysis → agent-safety review → scrapping 3 items that would break trained agents → implementing the remaining 13 with clear rationale for each. The decision to keep `Runtime.evaluate` intact (item 1.2 scrapped) was validated by Browser Use's #1 finding.

### 2. The Policy Enforcement Pipeline
`ShouldBlockRequest()` with pre-compiled domain matchers injected into `OnBeforeRequest()` BEFORE expensive handler/cookie setup. Returns `net::ERR_BLOCKED_BY_CLIENT` (standard Chrome error shape). This is real security infrastructure, not a stub.

### 3. The Element Reference System
`CefElementRefIndex` with `ParseRefId()` handling `@e5`, `e5`, and `5` formats. `BuildRefText()` generating `[1] @e1 button "Submit"` format. This is the foundation for agent-browser CLI ref commands and it's clean, thread-safe, and fast.

### 4. The Stealth Config
9 independently toggleable anti-detection patches, all using `Object.defineProperty` with `configurable: true`. Designed for `Page.addScriptToEvaluateOnNewDocument`. Each patch is a self-contained JS block with no dependencies. The WebGL vendor/renderer strings are real-world NVIDIA values.

### 5. The Action Trace System
Three levels of progressive disclosure (summary → detail → full). UTF-8 check/cross marks in output. Generation-tracked. This is exactly what Browser Use's "three-level hierarchical CLI" describes, and it's built into the browser layer where it can capture everything.

---

## What's Concerning

### 1. Build Fragility
Every CEF build attempt on a new machine requires re-discovering and re-applying ~15 API compatibility fixes (base::Value::Dict → base::DictValue, string_view conversions, FileEnumerator API, JSONReader args, FrameTreeNodeId, etc.). These fixes exist on the GPU cluster's build but were never committed because they modify files that exist in the upstream CEF fork.

**What should happen:** Create a `patch/chromium147-compat.patch` that captures ALL these fixes and auto-apply it as part of the build script. Or better: commit the fixed files to our fork.

### 2. Annotated Screenshot Pipeline Stripped
The full Skia annotation rendering (AnnotatedScreenshotHelper, WalkAXTree, EncodeAndSaveOnBlockingThread) was built but had to be stripped from the build because it uses `content/browser/accessibility/browser_accessibility.h` — an internal Chromium header not accessible from `libcef_static`.

**What should happen:** Re-implement using CDP `Accessibility.getFullAXTree` for the AX tree walk (public API, no internal headers needed). Keep the Skia rendering but feed it data from CDP instead of BrowserAccessibility pointers.

### 3. Test Suites Disabled
All 6 test suites (42 tests) are disabled because they `#include` internal `libcef/browser/` headers from the `ceftests` target, which links `libcef_dll_wrapper` (not `libcef_static`). The partition_alloc headers aren't available.

**What should happen:** Either create a separate `cef_internal_tests` GN target that links `libcef_static`, or rewrite the tests to use the public C++ API only.

### 4. No CEF Benchmark Numbers
After 4 days of work and 2 build machines, we still don't have our own perf numbers. We have numbers for agent-browser (14.9s), Playwright (127s), and CDP Raw (4.2s), but not for ourselves. This is because of the headless CDP message pump issue (concern #2 above).

### 5. The `cef_api_untracked.json` Hack
We're using a fake API version (999999) with the Linux 14700 hash. This works but it's a hack — the version manager should compute real hashes. Any API change will cause a hash mismatch at runtime.

---

## The Remote Build Machine State

**Location:** `/home/ubuntu/cef-build/` on `ec2-3-120-153-36.eu-central-1.compute.amazonaws.com`

```
chromium/src/                     # Chromium 147.0.7727.0
chromium/src/cef/                 # Our fork (copied from git, with build fixes applied)
chromium/src/out/Release_GN_x64/ # Partially compiled (~37k of ~44k objects done)
cef/                              # Clean clone of github.com/maceip/cef zero-user-native
depot_tools/                      # Fresh clone
```

**Installed frameworks:** agent-browser 0.22.3, browser-use 0.12.5, Playwright 1.58.0, Stagehand 3.2.0, Chrome DevTools MCP 0.20.3

**What's NOT committed from the remote:**
- All `base::Value::Dict → base::DictValue` renames
- `CefAuthProfileTraits` in `cef_types_wrappers.h`
- `cef_api_untracked.json` with version 999999
- `gpu/webgpu/DAWN_VERSION` + `dawn_commit_hash.h`
- Disabled mojo patch (`mojo_connect_result_3664.patch.disabled`)
- `.npmrc` fix for devtools-frontend (registry + omit=optional)
- browser_capture_impl.cc rewritten as clean stub
- All frame_impl.h/cc disconnect handler changes

---

## Next Steps (Prioritized)

1. **Fix the mojo disconnect cascade** using the lambda wrapper approach (keeps internal API stable)
2. **Get the build to succeed** on the EPYC machine
3. **Fix the headless CDP issue** (try Ozone headless backend)
4. **Get our benchmark numbers** — the whole point
5. **Commit the build fixes** to git so we stop re-discovering them
6. **Add proprietary codecs + PDF** to the GN args and rebuild
7. **Run Stagehand + browser-use benchmarks** on the EPYC machine
8. **Write the blog post** with the benchmark results
