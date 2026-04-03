#include "tests/cefheadless/headless_handler.h"

#include <utility>

#include "cef/libcef/browser/stealth_config.h"
#include "include/base/cef_logging.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "tests/shared/browser/main_message_loop_external_pump.h"

namespace {

HeadlessHandler* g_instance = nullptr;

}  // namespace

HeadlessHandler::HeadlessHandler(int width,
                                 int height,
                                 bool enable_stealth)
    : width_(width), height_(height), enable_stealth_(enable_stealth) {
  DCHECK(!g_instance);
  g_instance = this;
}

HeadlessHandler::~HeadlessHandler() {
  g_instance = nullptr;
}

// static
HeadlessHandler* HeadlessHandler::GetInstance() {
  return g_instance;
}

void HeadlessHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

  browser_list_.push_back(browser);

  const auto command_line = CefCommandLine::GetGlobalCommandLine();
  const std::string port =
      command_line->GetSwitchValue("remote-debugging-port").ToString();
  LOG(INFO) << "cefheadless browser created"
            << (port.empty() ? "" : " (CDP port " + port + ")");

  if (enable_stealth_) {
    ApplyStealthConfig(browser);
  }
}

bool HeadlessHandler::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  return false;
}

void HeadlessHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

  auto it = browser_list_.begin();
  for (; it != browser_list_.end(); ++it) {
    if ((*it)->IsSame(browser)) {
      browser_list_.erase(it);
      break;
    }
  }

  if (browser_list_.empty()) {
    if (auto* message_pump = client::MainMessageLoopExternalPump::Get()) {
      message_pump->Quit();
      return;
    }
    CefQuitMessageLoop();
  }
}

void HeadlessHandler::GetViewRect(CefRefPtr<CefBrowser> browser,
                                  CefRect& rect) {
  CEF_REQUIRE_UI_THREAD();
  rect = CefRect(0, 0, width_, height_);
}

void HeadlessHandler::OnPaint(CefRefPtr<CefBrowser> browser,
                              PaintElementType type,
                              const RectList& dirtyRects,
                              const void* buffer,
                              int width,
                              int height) {
  CEF_REQUIRE_UI_THREAD();
}

void HeadlessHandler::CloseAllBrowsers(bool force_close) {
  if (!CefCurrentlyOn(TID_UI)) {
    CefPostTask(
        TID_UI,
        CefCreateClosureTask(base::BindOnce(&HeadlessHandler::CloseAllBrowsers,
                                            CefRefPtr<HeadlessHandler>(this),
                                            force_close)));
    return;
  }

  if (browser_list_.empty()) {
    if (auto* message_pump = client::MainMessageLoopExternalPump::Get()) {
      message_pump->Quit();
      return;
    }
    CefQuitMessageLoop();
    return;
  }

  for (const auto& browser : browser_list_) {
    browser->GetHost()->CloseBrowser(force_close);
  }
}

void HeadlessHandler::ApplyStealthConfig(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

  auto params = CefDictionaryValue::Create();
  params->SetString("source", CefStealthConfig::Default().BuildStealthScript());
  const int id = browser->GetHost()->ExecuteDevToolsMethod(
      0, "Page.addScriptToEvaluateOnNewDocument", params);
  if (id == 0) {
    LOG(WARNING) << "cefheadless failed to register stealth script";
  }
}
