// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "tests/cefheadless/headless_app.h"

#include <glib-unix.h>
#include <signal.h>

#include "include/base/cef_logging.h"
#include "include/cef_command_line.h"
#include "tests/cefheadless/headless_handler.h"
#include "tests/shared/browser/main_message_loop_external_pump.h"

namespace {

gboolean OnSignalReceived(gpointer /*data*/) {
  if (auto* handler = HeadlessHandler::GetInstance()) {
    handler->CloseAllBrowsers(true);
  } else if (auto* message_loop = client::MainMessageLoopExternalPump::Get()) {
    message_loop->Quit();
  }
  return G_SOURCE_REMOVE;
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

  const bool use_external_message_pump =
      command_line->HasSwitch(client::switches::kExternalMessagePump);

  CefSettings settings;
#if !defined(CEF_USE_SANDBOX)
  settings.no_sandbox = true;
#endif
  settings.external_message_pump = use_external_message_pump;
  settings.windowless_rendering_enabled = true;

  const std::string debug_port =
      command_line->GetSwitchValue("remote-debugging-port");
  if (!debug_port.empty()) {
    settings.remote_debugging_port = std::stoi(debug_port);
  }

  std::unique_ptr<client::MainMessageLoop> message_loop;
  if (use_external_message_pump) {
    message_loop = client::MainMessageLoopExternalPump::Create();
  } else {
    message_loop = std::make_unique<client::MainMessageLoopStd>();
  }

  g_unix_signal_add(SIGINT, &OnSignalReceived, nullptr);
  g_unix_signal_add(SIGTERM, &OnSignalReceived, nullptr);

  if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
    return CefGetExitCode();
  }

  LOG(INFO) << "cefheadless running"
            << " port=" << settings.remote_debugging_port;

  message_loop->Run();

  CefShutdown();
  return 0;
}
