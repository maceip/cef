// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "tests/cefheadless/headless_app.h"

#include <string>

#include "include/cef_command_line.h"
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
#if defined(OS_LINUX)
  if (!command_line) {
    return;
  }

  if (!command_line->HasSwitch("enable-features")) {
    command_line->AppendSwitchWithValue("enable-features", "UseOzonePlatform");
  }
  if (!command_line->HasSwitch(client::switches::kOzonePlatform)) {
    command_line->AppendSwitchWithValue(client::switches::kOzonePlatform,
                                        "headless");
  }
  if (!command_line->HasSwitch("headless")) {
    command_line->AppendSwitch("headless");
  }
  if (!command_line->HasSwitch("remote-debugging-address")) {
    command_line->AppendSwitchWithValue("remote-debugging-address",
                                        "127.0.0.1");
  }
  if (!command_line->HasSwitch(client::switches::kOffScreenRenderingEnabled)) {
    command_line->AppendSwitch(client::switches::kOffScreenRenderingEnabled);
  }
  if (!command_line->HasSwitch("disable-gpu")) {
    command_line->AppendSwitch("disable-gpu");
  }
  if (!command_line->HasSwitch("disable-gpu-compositing")) {
    command_line->AppendSwitch("disable-gpu-compositing");
  }
#endif
}

void HeadlessApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();

  CefRefPtr<CefCommandLine> command_line =
      CefCommandLine::GetGlobalCommandLine();

  const std::string url =
      command_line->GetSwitchValue("url").empty()
          ? std::string("about:blank")
          : command_line->GetSwitchValue("url");

  const int width = GetIntSwitch(command_line, "width", 1280);
  const int height = GetIntSwitch(command_line, "height", 720);
  const bool enable_stealth = command_line->HasSwitch("stealth");

  handler_ = new HeadlessHandler(width, height, enable_stealth);

  CefWindowInfo window_info;
  window_info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  window_info.SetAsWindowless(static_cast<cef_window_handle_t>(0));

  CefBrowserSettings browser_settings;
  browser_settings.windowless_frame_rate = 30;

  CefBrowserHost::CreateBrowser(window_info, handler_, url, browser_settings,
                                nullptr, nullptr);
}

void HeadlessApp::OnScheduleMessagePumpWork(int64_t delay_ms) {
  if (auto* message_pump = client::MainMessageLoopExternalPump::Get()) {
    message_pump->OnScheduleMessagePumpWork(delay_ms);
  }
}
