---
name: sandwich_architecture
description: Three-layer architecture decision — Rust agent binary on cef-rs on our patched libcef.so
type: project
---

Architecture is a three-layer sandwich:

1. **cefagent (Rust)** — our agent binary built with cef-rs. External message pump, stealth injection, auth vault, snapshot formatter, element refs, CDP always responsive. This is what ships.

2. **cef-rs (Tauri)** — their auto-generated safe Rust bindings + platform sandboxing + wgpu OSR. We use this as-is (fork if needed for API version matching).

3. **libcef.so (our C++ fork)** — our Chromium patches compiled into the shared library. Policy enforcement in request pipeline, AX caching, dirty tracking, state journal, passkey support. Drop-in replacement for upstream libcef.so.

**Why:** cef-rs handles all the platform boilerplate (sandboxing, helper processes, library loading, GPU texture sharing). We focus on the Chromium-level patches that make it an agent browser. The Rust layer is where the agent-facing features live.

**How to apply:** All new agent-browser features go in Rust (cefagent crate). Only Chromium-internal patches go in C++ (our CEF fork). cef-rs is upstream — don't modify it unless necessary for API version compatibility.

**Key pattern from cef-rs:** External message pump (`do_message_loop_work()` in a loop) solves the headless CDP problem. Their OSR example proves it works.
