// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "tests/cefheadless/headless_app.h"

#include <cstdlib>
#include <string>

#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"
#include "tests/shared/common/client_switches.h"

namespace {

int GetIntSwitch(CefRefPtr<CefCommandLine> command_line,
                 const char* name,
                 int default_value) {
  const std::string value = command_line->GetSwitchValue(name);
  if (value.empty()) {
    return default_value;
  }
  return std::max(1, std::atoi(value.c_str()));
}

}  // namespace

HeadlessApp::HeadlessApp() = default;

void HeadlessApp::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
  if (!command_line || !process_type.empty()) {
    return;
  }

  // Use Chrome's native --headless mode. This creates a virtual display
  // surface internally so the compositor has a real frame sink. The DevTools
  // agent host initializes normally and /json responds immediately.
  //
  // DO NOT use Alloy OSR (SetAsWindowless) for headless CDP. OSR without a
  // display creates a browser with no compositor surface. The DevTools HTTP
  // server starts (TCP port binds) but the agent host never fully initializes
  // because there is no render frame until OnPaint starts producing frames —
  // which requires a working compositor. Result: /json hangs forever.
  command_line->AppendSwitch("headless");
  command_line->AppendSwitch("disable-gpu");
  command_line->AppendSwitch("no-sandbox");

#if defined(OS_LINUX)
  // Ozone headless platform — no X11/Wayland needed.
  if (!command_line->HasSwitch("enable-features")) {
    command_line->AppendSwitchWithValue("enable-features", "UseOzonePlatform");
  }
  if (!command_line->HasSwitch(client::switches::kOzonePlatform)) {
    command_line->AppendSwitchWithValue(client::switches::kOzonePlatform,
                                       "headless");
  }
#endif

  // CDP remote debugging.
  if (!command_line->HasSwitch("remote-debugging-port")) {
    command_line->AppendSwitchWithValue("remote-debugging-port", "9222");
  }
  if (!command_line->HasSwitch("remote-debugging-address")) {
    command_line->AppendSwitchWithValue("remote-debugging-address",
                                       "127.0.0.1");
  }
}

void HeadlessApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();

  CefRefPtr<CefCommandLine> command_line =
      CefCommandLine::GetGlobalCommandLine();

  const std::string url_value = command_line->GetSwitchValue("url").ToString();
  const std::string url = url_value.empty() ? "about:blank" : url_value;

  const int width = GetIntSwitch(command_line, "width", 1280);
  const int height = GetIntSwitch(command_line, "height", 720);
  const bool enable_stealth = command_line->HasSwitch("stealth");

  handler_ = new HeadlessHandler(width, height, enable_stealth);

  // Chrome runtime with normal window — NOT Alloy OSR.
  // In headless mode Chrome provides a virtual compositor surface,
  // so DevTools works correctly without a real display.
  CefWindowInfo window_info;
  window_info.runtime_style = CEF_RUNTIME_STYLE_DEFAULT;

  CefBrowserSettings browser_settings;

  CefBrowserHost::CreateBrowser(window_info, handler_, url, browser_settings,
                                nullptr, nullptr);
}
