# CEF Agent-Browser Project Journal
**Date:** 2026-03-31
**Branch:** `cursor/journal-action-items-be52` on `github.com/maceip/cef`

---

## Where We Are

### Fixes Completed This Turn
- **Mojo disconnect build blocker fixed in-tree**
  - `libcef/renderer/frame_impl.cc` now uses Chromium's upstream
    `set_disconnect_with_reason_handler` API.
  - Internal CEF disconnect plumbing still receives a `MojoResult` by wrapping
    the 2-arg callback and passing `MOJO_RESULT_OK`, preserving existing retry
    and logging logic.
  - `patch/patch.cfg` no longer applies the stale
    `mojo_connect_result_3664` patch, so local patch application matches the
    current source tree instead of reintroducing a custom Mojo API.

- **Automation program translator blocker fixed**
  - `InstructionType` was moved out of `CefAutomationProgram` and replaced with
    a top-level translator-safe enum:
    `cef_automation_instruction_type_t`.
  - `CefBrowserHost::ExecuteAutomationProgram(...)` is now declared in the
    public API instead of only existing on `CefBrowserHostBase`.
  - A browser-level regression test was added in
    `tests/ceftests/automation_program_unittest.cc`.

- **Stale browser capture test updated**
  - `tests/ceftests/browser_capture_unittest.cc` previously expected
    screenshot-scaffold failure text even though
    `CaptureAnnotatedScreenshot()` now runs a real screenshot pipeline.
  - The test now expects a successful callback with an empty error string and
    the requested output path.

### Validation Completed
- Reproduced the original automation translator failure before the fix:
  - `Exception: Failed to translate type: InstructionType`
- Re-ran the translator after the enum/public API fix and confirmed that it
  progressed past the original failure and generated automation/browser host
  C API headers before stopping later on an unrelated `VERSION.stamp` issue.
- Verified helper script shell syntax:
  - `bash -n tools/claude/build.sh`
  - `bash -n tools/claude/test-perf.sh`
  - `bash -n tools/patch.sh`

### Commits Added This Turn
- `8d9932e58` - **Use upstream mojo disconnect handlers**
- `b66073ffe` - **Expose automation program API publicly**

---

## Where We Are Stuck

### 1. Headless CDP Benchmarking Is Still Not Proven End-to-End

The original "CEF headless CDP message pump" concern is still partially open.
This branch now has useful pieces in place:

- Linux `cefsimple` already supports `--external-message-pump`
- shared browser test infrastructure already supports external pump scheduling
- the repo already has Linux-side Ozone switch plumbing in shared client code

What is still missing is a single validated end-to-end benchmark/sample path
that proves:

1. headless/browserless startup,
2. stable UI thread pumping,
3. stable CDP request/response flow,
4. benchmark harness execution against that mode.

### 2. Annotated Screenshots Still Depend on Internal Accessibility Headers

`libcef/browser/browser_capture_impl.cc` still includes:

- `content/browser/accessibility/browser_accessibility.h`
- `content/browser/accessibility/browser_accessibility_manager.h`

So the earlier architectural concern remains valid: the screenshot pipeline is
implemented, but it still depends on internal Chromium accessibility types
instead of a public CDP-based AX walk.

### 3. Translator/Wrapper Generation Flow Needs a Clean Build Environment Pass

The specific translator parse blocker is fixed, but a full translator run in
this workspace still stops later with:

- `ERROR: while processing /workspace/VERSION.stamp`

That is no longer an automation-header parsing problem, but it does mean a
clean generation pass still needs to happen in a proper build/gen environment
before treating wrapper output as fully refreshed.

---

## What's Concerning

### 1. Journal/Branch Drift Was Real

The original journal entry described a branch state that no longer matched the
current checkout:

- the Mojo disconnect logic in `frame_impl` had already evolved
- `JOURNAL.md` itself was not present in the working tree
- one capture test still described old scaffold behavior

This kind of drift makes it easy to chase solved problems or preserve stale
blockers in docs.

### 2. Build Helper Scripts Still Need Branch-Aware Hardening

`tools/claude/build.sh` and related helpers are usable, but they still assume a
fairly ideal local Chromium layout and do not yet encode:

- translator regeneration as an explicit step,
- release args for codecs/PDF follow-up work,
- a documented headless Ozone benchmark path,
- or branch-specific compatibility assertions.

### 3. Screenshot Success Does Not Yet Mean AX Ref Quality Is Verified

The updated capture test now matches the real callback contract for successful
screenshots. However, it still does not prove:

- that AX refs are populated,
- that labels are rendered,
- or that internal AX access is stable across all test environments.

That is a reasonable tradeoff for now, but it is still a gap.

---

## Next Steps (Prioritized)

1. **Prove the headless CDP path end-to-end**
   - Add or update a minimal sample/benchmark invocation path using external
     message pump plus Linux headless/Ozone flags.
   - Capture one successful DevTools round-trip in that mode.

2. **Run a clean translator/wrapper generation pass in a proper build tree**
   - The nested enum blocker is fixed.
   - Next step is to regenerate wrappers/C API outputs cleanly after the
     environment-specific `VERSION.stamp` issue is addressed.

3. **Replace internal AX screenshot dependency with a CDP-based AX walk**
   - Implement the journal's original architectural plan:
     `Accessibility.getFullAXTree` feeding the existing annotation renderer.

4. **Harden build helper scripts**
   - Add explicit notes/targets for:
     - translator regeneration,
     - perf-only test suites,
     - headless benchmark invocation,
     - release args for codecs/PDF.

5. **Expand focused validation**
   - Run `automation_program_unittest`,
     `browser_capture_unittest`, and perf-focused suites in a real build tree.
   - Add stronger screenshot assertions once the environment can reliably expose
     accessibility data.

---

## Notes for Future Iteration

- The highest-value blocker resolved in this turn was the mismatch between the
  source tree and the old custom Mojo patch. That was both a build problem and
  a patch-workflow problem.
- The highest-value API unblock resolved in this turn was the automation
  translator failure. The parser no longer fails on the nested enum design.
