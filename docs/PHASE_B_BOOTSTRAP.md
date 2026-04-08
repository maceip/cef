# Phase B Bootstrap

## Scope

This document captures the **on-ramp to Phase B (build-error fixing)** from the current environment state.

## Entry Context

Phase A produced a patch baseline and failure inventory (see Phase A gate document). The next step is to bootstrap build-error capture.

## Bootstrap Attempts Performed

### Attempt 1 — direct baseline build capture

Command:

```bash
autoninja -k 0 -C out/Debug_GN_x64 cef > tools/claude/build_output.txt 2>&1
```

Result:

- Failed immediately with exit code `127` (tool unavailable in PATH).

### Attempt 2 — build environment setup

Command:

```bash
CHROMIUM_DIR=/tmp/phase_ab_worktree ./tools/claude/build.sh setup > tools/claude/build_setup_output.txt 2>&1
```

Result excerpt:

- `Fetching depot_tools...`
- `python3_bin_reldir.txt not found...`
- `ERROR: gn not found.`

### Attempt 3 — depot_tools initialization + explicit PATH

Commands:

```bash
/home/ubuntu/depot_tools/update_depot_tools
PATH=/home/ubuntu/depot_tools:$PATH CHROMIUM_DIR=/tmp/phase_ab_worktree ./tools/claude/build.sh setup
```

Result excerpt:

- `gn.py: Unable to find gn in your $PATH`
- `Hint: which -a gn should output two entries`

### Attempt 4 — `autoninja` via depot_tools in PATH

Command:

```bash
PATH=/home/ubuntu/depot_tools:$PATH autoninja -k 0 -C out/Debug_GN_x64 cef > tools/claude/build_output.txt 2>&1
```

Result excerpt:

- `depot_tools/ninja.py: Could not find Ninja in the third_party of the current project, nor in your PATH.`
- `Please take one of the following actions to install Ninja:`
- `- If your project has DEPS, add a CIPD Ninja dependency to DEPS.`
- `- Otherwise, add Ninja to your PATH *after* depot_tools.`

### Attempt 5 — install system Ninja/GN and retry baseline build

Commands:

```bash
sudo apt-get install -y ninja-build generate-ninja
PATH=/home/ubuntu/depot_tools:$PATH autoninja -k 0 -C out/Debug_GN_x64 cef > tools/claude/build_output.txt 2>&1
```

Result excerpt:

- `ninja: Entering directory \`out/Debug_GN_x64'`
- `ninja: fatal: chdir to 'out/Debug_GN_x64' - No such file or directory`

### Attempt 6 — retry setup after GN/Ninja install

Command:

```bash
PATH=/home/ubuntu/depot_tools:$PATH CHROMIUM_DIR=/tmp/phase_ab_worktree ./tools/claude/build.sh setup > tools/claude/build_setup_output.txt 2>&1
```

Result excerpt:

- `ERROR Can't find source root.`
- `I could not find a ".gn" file in the current directory or any parent`
- `ERROR: gn not found. Add depot_tools to PATH: export PATH=$HOME/depot_tools:$PATH`

### Attempt 7 — direct `gn gen` with explicit root

Command:

```bash
PATH=/home/ubuntu/depot_tools:$PATH gn gen out/Debug_GN_x64 --root=/workspace > tools/claude/gn_gen_output.txt 2>&1
```

Result excerpt:

- `ERROR Could not load dotfile.`
- `The file "/workspace/.gn" couldn't be loaded`

## Bootstrap Outcome

The Phase B capture flow is now clearly defined and partially wired. Tooling improved enough to invoke Ninja, but the build cannot progress because this workspace is not a full Chromium source-root layout (missing generated out dir and expected source-root semantics for GN setup).

## Required Preconditions for Phase B Execution

1. Full Chromium checkout exists (not CEF-only tree).
2. CEF is integrated as `chromium/src/cef`.
3. GN and autoninja are operational for that checkout.
4. Build output dir generated (`out/Debug_GN_x64` or platform equivalent) from the real Chromium source root.

## First Commands Once Preconditions Are Met

```bash
autoninja -k 0 -C out/Debug_GN_x64 cef 2>&1 | tee cef/tools/claude/build_output.txt
python3 cef/tools/claude/analyze_build_output.py cef/tools/claude/build_output.txt \
  --old-version 146.0.7680.0 \
  --new-version 147.0.7727.0 \
  --no-color > cef/tools/claude/build_analysis.txt
```

## Initial Prioritization Strategy for Phase B

When `build_analysis.txt` exists:

1. Fix highest-error-count files first.
2. Rebuild per-file object targets after each fix.
3. Defer full target rebuild until all listed files compile.
4. Re-run full build, re-analyze, repeat.

## Current Bootstrap Artifacts

- `tools/claude/build_output.txt` (captured command failure output)
- `tools/claude/build_analysis.txt` (captured analyzer limitation note for this bootstrap state)
- `tools/claude/build_setup_output.txt` (latest setup failure output)
- `tools/claude/gn_gen_output.txt` (explicit GN generation failure output)
- `tools/claude/patch_output.txt`
- `tools/claude/patch_analysis.txt`

## Runtime Evidence (terminal excerpts)

Patch baseline:

```text
--> Reading patch config /tmp/phase_ab_worktree/cef/patch/patch.cfg
...
!!!! WARNING: Failed to apply gn_config, fix manually and run with --resave
...
!!!! WARNING: Failed to apply chrome_browser_context_menus, fix manually and run with --resave
```

Build bootstrap:

```text
$ autoninja -k 0 -C out/Debug_GN_x64 cef
bash: autoninja: command not found
```

```text
==> Setting up CEF build environment
gn.py: Unable to find gn in your $PATH
Hint: `which -a gn` should output two entries
ERROR: gn not found. Add depot_tools to PATH: export PATH=$HOME/depot_tools:$PATH
```

```text
$ autoninja -k 0 -C out/Debug_GN_x64 cef
--: line 1: autoninja: command not found
```

```text
$ PATH=/home/ubuntu/depot_tools:$PATH autoninja -k 0 -C out/Debug_GN_x64 cef
depot_tools/ninja.py: Could not find Ninja in the third_party of the current project, nor in your PATH.
Please take one of the following actions to install Ninja:
- If your project has DEPS, add a CIPD Ninja dependency to DEPS.
- Otherwise, add Ninja to your PATH *after* depot_tools.
```

```text
$ PATH=/home/ubuntu/depot_tools:$PATH autoninja -k 0 -C out/Debug_GN_x64 cef
ninja: Entering directory `out/Debug_GN_x64'
ninja: fatal: chdir to 'out/Debug_GN_x64' - No such file or directory
```

```text
==> Setting up CEF build environment
ERROR Can't find source root.
I could not find a ".gn" file in the current directory or any parent,
and the --root command-line argument was not specified.
ERROR: gn not found. Add depot_tools to PATH: export PATH=$HOME/depot_tools:$PATH
```

```text
$ PATH=/home/ubuntu/depot_tools:$PATH gn gen out/Debug_GN_x64 --root=/workspace
ERROR Could not load dotfile.
The file "/workspace/.gn" couldn't be loaded
```
