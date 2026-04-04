// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "tests/cefheadless/headless_app.h"

#include <signal.h>

#include "include/base/cef_logging.h"
#include "include/cef_command_line.h"
#include "tests/cefheadless/headless_handler.h"

namespace {

void OnSignalReceived(int /*signal*/) {
  if (auto* handler = HeadlessHandler::GetInstance()) {
    handler->CloseAllBrowsers(true);
  }
}

}  // namespace

NO_STACK_PROTECTOR
int main(int argc, char* argv[]) {
  CefMainArgs main_args(argc, argv);

  CefRefPtr<HeadlessApp> app(new HeadlessApp);

  int exit_code = CefExecuteProcess(main_args, app, nullptr);
  if (exit_code >= 0) {
    return exit_code;
  }

  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
  command_line->InitFromArgv(argc, argv);

  CefSettings settings;
  settings.no_sandbox = true;

  // DO NOT set windowless_rendering_enabled = true.
  // Chrome runtime headless mode uses a virtual display compositor,
  // not CEF's OSR (windowless) mode. Setting this to true forces the
  // Alloy OSR path which has no compositor surface and hangs on CDP.
  settings.windowless_rendering_enabled = false;

  // Standard message loop — no external pump needed.
  settings.multi_threaded_message_loop = false;
  settings.external_message_pump = false;

  // Suppress verbose logging.
  settings.log_severity = LOGSEVERITY_WARNING;

  // Signal handling for clean shutdown.
  signal(SIGINT, OnSignalReceived);
  signal(SIGTERM, OnSignalReceived);

  if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
    return CefGetExitCode();
  }

  const auto cdp_port =
      command_line->GetSwitchValue("remote-debugging-port").ToString();
  LOG(WARNING) << "cefheadless running, CDP at http://127.0.0.1:"
               << (cdp_port.empty() ? "9222" : cdp_port) << "/json";

  // Standard CEF message loop. --headless + ozone-platform=headless
  // provides a virtual display, so the compositor works and DevTools
  // agent host initializes normally.
  CefRunMessageLoop();

  CefShutdown();
  return 0;
}
