#ifndef CEF_TESTS_CEFHEADLESS_HEADLESS_APP_H_
#define CEF_TESTS_CEFHEADLESS_HEADLESS_APP_H_
#pragma once

#include "include/cef_app.h"
#include "tests/cefheadless/headless_handler.h"

class HeadlessApp : public CefApp, public CefBrowserProcessHandler {
 public:
  HeadlessApp();

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override;
  void OnContextInitialized() override;

 private:
  CefRefPtr<HeadlessHandler> handler_;

  IMPLEMENT_REFCOUNTING(HeadlessApp);
};

#endif  // CEF_TESTS_CEFHEADLESS_HEADLESS_APP_H_
