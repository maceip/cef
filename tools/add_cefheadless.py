#!/usr/bin/env python3
"""Add cefheadless target to an upstream CEF checkout.

Usage: python3 add_cefheadless.py <cef_src_dir> <fork_dir>

Copies cefheadless sources from fork_dir into cef_src_dir and patches
BUILD.gn + cef_paths2.gypi to register the target. Also patches
menu_runner_views_aura.cc to gate X11 includes on USE_X11.
"""
import os
import re
import shutil
import sys


def main():
    cef_src = sys.argv[1]  # e.g. /home/runner/cef-build/chromium/src/cef
    fork_dir = sys.argv[2]  # e.g. cef-src (our fork checkout)

    # 1. Copy cefheadless sources
    dst = os.path.join(cef_src, "tests", "cefheadless")
    os.makedirs(dst, exist_ok=True)
    src = os.path.join(fork_dir, "tests", "cefheadless")
    for f in os.listdir(src):
        if f.endswith((".cc", ".h")):
            shutil.copy2(os.path.join(src, f), os.path.join(dst, f))
            print(f"  Copied {f}")

    # 2. Add cefheadless entries to cef_paths2.gypi
    gypi_path = os.path.join(cef_src, "cef_paths2.gypi")
    gypi = open(gypi_path).read()
    if "cefheadless_sources" not in gypi:
        insert = """
    'cefheadless_sources_common': [
      'tests/cefheadless/headless_app.cc',
      'tests/cefheadless/headless_app.h',
      'tests/cefheadless/headless_handler.cc',
      'tests/cefheadless/headless_handler.h',
    ],
    'cefheadless_sources_linux': [
      'tests/shared/browser/main_message_loop.cc',
      'tests/shared/browser/main_message_loop.h',
      'tests/shared/browser/main_message_loop_external_pump.cc',
      'tests/shared/browser/main_message_loop_external_pump.h',
      'tests/shared/browser/main_message_loop_external_pump_linux.cc',
      'tests/shared/browser/main_message_loop_std.cc',
      'tests/shared/browser/main_message_loop_std.h',
      'tests/shared/common/client_switches.cc',
      'tests/shared/common/client_switches.h',
      'tests/cefheadless/cefheadless_linux.cc',
    ],"""
        # Insert before the last closing brace+newline
        gypi = gypi.rstrip()
        if gypi.endswith("}"):
            gypi = gypi[:-1] + insert + "\n}"
        open(gypi_path, "w").write(gypi)
        print("  Added cefheadless entries to cef_paths2.gypi")

    # 3. Add cefheadless executable target to BUILD.gn
    build_gn = os.path.join(cef_src, "BUILD.gn")
    bg = open(build_gn).read()
    if 'executable("cefheadless")' not in bg:
        target = '''

# cefheadless target (added by add_cefheadless.py)
executable("cefheadless") {
  testonly = true
  sources = includes_common +
            gypi_paths2.includes_wrapper +
            gypi_paths2.cefheadless_sources_common
  deps = [
    ":libcef",
    ":libcef_dll_wrapper",
  ]
  defines = [ "CEF_USE_SANDBOX" ]
  use_libcxx_modules = false
  if (is_linux) {
    sources += includes_linux +
               gypi_paths2.cefheadless_sources_linux
    if (!is_component_build) {
      configs += [ "//build/config/gcc:rpath_for_built_shared_libraries" ]
    }
  }
}
'''
        open(build_gn, "a").write(target)
        print("  Added cefheadless executable target to BUILD.gn")

    # 4. Add cefheadless to cef group deps
    if '":cefheadless"' not in bg:
        bg = open(build_gn).read()  # re-read after append
        bg = bg.replace('":cefsimple",', '":cefsimple",\n    ":cefheadless",')
        open(build_gn, "w").write(bg)
        print("  Added :cefheadless to cef group deps")

    # 5. Patch menu_runner_views_aura.cc to gate X11 on USE_X11
    aura = os.path.join(cef_src, "libcef", "browser", "native",
                        "menu_runner_views_aura.cc")
    if os.path.exists(aura):
        code = open(aura).read()
        if '#include "ui/gfx/x/connection.h"' in code and "USE_X11" not in code:
            code = code.replace(
                '#include "ui/gfx/x/connection.h"',
                '#if defined(USE_X11)\n#include "ui/gfx/x/connection.h"\n#endif'
            )
            code = code.replace(
                '#if BUILDFLAG(IS_LINUX)\n  if (browser->IsWindowless()',
                '#if BUILDFLAG(IS_LINUX) && defined(USE_X11)\n  if (browser->IsWindowless()'
            )
            open(aura, "w").write(code)
            print("  Patched menu_runner_views_aura.cc for USE_X11")

    print("Done.")


if __name__ == "__main__":
    main()
