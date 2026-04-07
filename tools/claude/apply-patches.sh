#!/bin/bash
# Automated CEF patch application — applies patches one-by-one and skips
# conflicts (likely upstreamed features). Run with --help for usage.

set -euo pipefail

CEF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="$(dirname "$CEF_DIR")"
PATCH_DIR="$CEF_DIR/patch/patches"
PATCH_CFG="$CEF_DIR/patch/patch.cfg"
DEPOT_TOOLS="${DEPOT_TOOLS:-$HOME/depot_tools}"
export PATH="$DEPOT_TOOLS:$PATH"

# State file tracking which patches were skipped
SKIP_FILE="$CEF_DIR/.patch-skipped"
APPLIED_FILE="$CEF_DIR/.patch-applied"

log() { echo "[patches] $*"; }
warn() { echo "[patches] WARNING: $*" >&2; }
err() { echo "[patches] ERROR: $*" >&2; }

# Parse patch.cfg to get patch list
get_patches() {
  python3 -c "
import ast, sys
with open('$PATCH_CFG') as f:
    content = f.read()
# Find the patches list
start = content.index('patches = [')
# Extract just the list
bracket_content = content[start + len('patches = '):]
patches = ast.literal_eval(bracket_content)
for p in patches:
    name = p.get('name', '')
    path = p.get('path', '.')
    condition = p.get('condition', '')
    print(f'{name}|{path}|{condition}')
" 2>/dev/null
}

# Apply a single patch, return 0 on success, 1 on conflict
apply_patch() {
  local patch_name="$1"
  local patch_path="$2"
  local patch_file="$PATCH_DIR/${patch_name}.patch"
  local target_dir="$SRC_DIR"

  if [ "$patch_path" != "." ]; then
    target_dir="$SRC_DIR/$patch_path"
  fi

  if [ ! -f "$patch_file" ]; then
    warn "Patch file not found: $patch_file"
    return 1
  fi

  # Try applying the patch
  if cd "$target_dir" && git apply --check "$patch_file" 2>/dev/null; then
    git apply "$patch_file" 2>/dev/null
    return 0
  else
    # Patch doesn't apply cleanly
    return 1
  fi
}

cmd_apply() {
  local dry_run="${1:-}"

  log "Applying CEF patches to Chromium source..."
  log "Source dir: $SRC_DIR"
  log "Patch dir: $PATCH_DIR"

  > "$SKIP_FILE" 2>/dev/null || true
  > "$APPLIED_FILE" 2>/dev/null || true

  local total=0 applied=0 skipped=0

  while IFS='|' read -r name path condition; do
    [ -z "$name" ] && continue
    total=$((total + 1))

    # Check condition
    if [ -n "$condition" ] && [ -z "${!condition:-}" ]; then
      log "  SKIP (condition '$condition' not set): $name"
      echo "$name|condition" >> "$SKIP_FILE"
      skipped=$((skipped + 1))
      continue
    fi

    if [ "$dry_run" = "--dry-run" ]; then
      local patch_file="$PATCH_DIR/${name}.patch"
      local target_dir="$SRC_DIR"
      [ "$path" != "." ] && target_dir="$SRC_DIR/$path"

      if [ -f "$patch_file" ] && cd "$target_dir" && git apply --check "$patch_file" 2>/dev/null; then
        log "  WOULD APPLY: $name"
      else
        log "  WOULD SKIP (conflict): $name"
      fi
      continue
    fi

    if apply_patch "$name" "$path"; then
      log "  APPLIED: $name"
      echo "$name" >> "$APPLIED_FILE"
      applied=$((applied + 1))
    else
      # Patch doesn't apply — likely already upstreamed
      warn "  CONFLICT (skipped, likely upstreamed): $name"
      echo "$name|conflict" >> "$SKIP_FILE"
      skipped=$((skipped + 1))
    fi
  done < <(get_patches)

  log ""
  log "Results: $applied applied, $skipped skipped (of $total total)"

  if [ -s "$SKIP_FILE" ]; then
    log ""
    log "Skipped patches (see $SKIP_FILE):"
    cat "$SKIP_FILE" | while IFS='|' read -r name reason; do
      log "  - $name ($reason)"
    done
  fi
}

cmd_revert() {
  log "Reverting all CEF patches..."
  cd "$SRC_DIR"
  python3 "$CEF_DIR/tools/patch_updater.py" --revert 2>&1
  log "Done."
}

cmd_status() {
  log "Patch status:"
  if [ -f "$APPLIED_FILE" ]; then
    log "  Applied:"
    cat "$APPLIED_FILE" | while read -r name; do
      log "    + $name"
    done
  fi
  if [ -f "$SKIP_FILE" ]; then
    log "  Skipped:"
    cat "$SKIP_FILE" | while IFS='|' read -r name reason; do
      log "    - $name ($reason)"
    done
  fi
}

cmd_help() {
  echo "CEF Patch Management"
  echo ""
  echo "Usage: ./tools/claude/apply-patches.sh [command]"
  echo ""
  echo "Commands:"
  echo "  (none)      Apply all patches with conflict detection"
  echo "  --dry-run   Show what would happen"
  echo "  --revert    Revert all patches"
  echo "  --status    Show applied/skipped patches"
  echo "  --help      This message"
}

case "${1:---apply}" in
  --dry-run)  cmd_apply --dry-run ;;
  --revert)   cmd_revert ;;
  --status)   cmd_status ;;
  --help)     cmd_help ;;
  --apply|*)  cmd_apply ;;
esac
