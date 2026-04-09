// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "tests/cefheadless/headless_app.h"

#include <memory>
#include <signal.h>

#include "include/base/cef_logging.h"
#include "include/cef_command_line.h"
#include "tests/cefheadless/headless_handler.h"
#include "tests/shared/browser/main_message_loop_external_pump.h"
#include "tests/shared/browser/main_message_loop_std.h"
#include "tests/shared/browser/main_message_loop.h"

namespace {

constexpr char kCdpStdMessageLoopSwitch[] = "cdp-std-message-loop";

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
  const bool bad_windowless_mode =
      command_line->HasSwitch("cdp-bad-windowless");
  const bool std_message_loop =
      command_line->HasSwitch(kCdpStdMessageLoopSwitch);

  CefSettings settings;
#if !defined(CEF_USE_SANDBOX)
  settings.no_sandbox = true;
#endif

  // DO NOT set windowless_rendering_enabled = true.
  // Chrome runtime headless mode uses a virtual display compositor,
  // not CEF's OSR (windowless) mode. Setting this to true forces the
  // Alloy OSR path which has no compositor surface and hangs on CDP.
  settings.windowless_rendering_enabled = bad_windowless_mode;

  // Use Chromium's native message loop on a dedicated thread. This avoids
  // the external pump (GLib idle-priority source + pipe wakeup) which adds
  // latency to every CDP round-trip. CDP events like domContentEventFired
  // fire immediately instead of waiting for GLib poll.
  // Pass --cdp-std-message-loop to fall back to the old external pump for
  // A/B tracing.
  if (std_message_loop) {
    // Legacy: external pump for A/B comparison
    settings.multi_threaded_message_loop = false;
    settings.external_message_pump = true;
  } else {
    // Fast path: Chromium's own message loop, no external pump overhead
    settings.multi_threaded_message_loop = true;
    settings.external_message_pump = false;
  }

  // Suppress verbose logging.
  settings.log_severity = LOGSEVERITY_WARNING;
  LOG(WARNING) << "CDPTRACE_HEADLESS_SETTINGS"
               << " windowless_rendering_enabled="
               << settings.windowless_rendering_enabled
               << " multi_threaded_message_loop="
               << settings.multi_threaded_message_loop
               << " external_message_pump=" << settings.external_message_pump
               << " cdp_bad_windowless=" << bad_windowless_mode
               << " cdp_std_message_loop=" << std_message_loop;

  std::unique_ptr<client::MainMessageLoop> message_loop;
  if (settings.multi_threaded_message_loop) {
    // With multi_threaded_message_loop, CEF runs its own message loop
    // internally. We just need a simple loop that blocks until shutdown.
    message_loop = std::make_unique<client::MainMessageLoopStd>();
  } else if (settings.external_message_pump) {
    message_loop = client::MainMessageLoopExternalPump::Create();
  } else {
    message_loop = std::make_unique<client::MainMessageLoopStd>();
  }

  // Signal handling for clean shutdown.
  signal(SIGINT, OnSignalReceived);
  signal(SIGTERM, OnSignalReceived);

  if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
    LOG(WARNING) << "CDPTRACE_HEADLESS_CEF_INITIALIZE_FAILED exit_code="
                 << CefGetExitCode();
    return CefGetExitCode();
  }

  const auto cdp_port =
      command_line->GetSwitchValue("remote-debugging-port").ToString();
  LOG(WARNING) << "cefheadless running, CDP at http://127.0.0.1:"
               << (cdp_port.empty() ? "9222" : cdp_port) << "/json";
  LOG(WARNING) << "CDPTRACE_HEADLESS_RUNTIME_SWITCHES"
               << " user_data_dir="
               << command_line->GetSwitchValue("user-data-dir").ToString()
               << " ozone_platform="
               << command_line->GetSwitchValue("ozone-platform").ToString()
               << " has_headless=" << command_line->HasSwitch("headless")
               << " remote_debugging_address="
               << command_line->GetSwitchValue("remote-debugging-address")
                      .ToString();

  const int run_result = message_loop->Run();

  LOG(WARNING) << "CDPTRACE_HEADLESS_MESSAGE_LOOP_EXIT result=" << run_result;
  CefShutdown();
  return 0;
}
