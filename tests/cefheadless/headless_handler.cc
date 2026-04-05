#include "tests/cefheadless/headless_handler.h"

#include <sstream>
#include <string>

#include "include/base/cef_logging.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace {

HeadlessHandler* g_instance = nullptr;

std::string GetDataURI(const std::string& data, const std::string& mime_type) {
  return "data:" + mime_type + ";base64," +
         CefURIEncode(CefBase64Encode(data.data(), data.size()), false)
             .ToString();
}

std::string BuildDefaultStealthScript() {
  return R"JS(
Object.defineProperty(navigator, 'webdriver', {
  get: () => undefined,
  configurable: true,
});
(function() {
  const props = Object.getOwnPropertyNames(window);
  for (const prop of props) {
    if (prop.match(/^(cdc_|__webdriver_|\$cdc_)/)) {
      try { delete window[prop]; } catch(e) {}
    }
  }
})();
)JS";
}

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
    ApplyStealth(browser);
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
    CefQuitMessageLoop();
  }
}

void HeadlessHandler::OnLoadError(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  ErrorCode errorCode,
                                  const CefString& errorText,
                                  const CefString& failedUrl) {
  CEF_REQUIRE_UI_THREAD();

  if (errorCode == ERR_ABORTED) {
    return;
  }

  std::stringstream ss;
  ss << "<html><body bgcolor=\"white\">"
        "<h2>Failed to load URL "
     << std::string(failedUrl) << " with error " << std::string(errorText)
     << " (" << errorCode << ").</h2></body></html>";

  frame->LoadURL(GetDataURI(ss.str(), "text/html"));
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
    CefQuitMessageLoop();
    return;
  }

  for (const auto& browser : browser_list_) {
    browser->GetHost()->CloseBrowser(force_close);
  }
}

void HeadlessHandler::ApplyStealth(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

  auto params = CefDictionaryValue::Create();
  params->SetString("source", BuildDefaultStealthScript());
  const int id = browser->GetHost()->ExecuteDevToolsMethod(
      0, "Page.addScriptToEvaluateOnNewDocument", params);
  if (id == 0) {
    LOG(WARNING) << "cefheadless failed to register stealth script";
  }
}
