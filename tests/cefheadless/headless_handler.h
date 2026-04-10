// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#ifndef CEF_TESTS_CEFHEADLESS_HEADLESS_HANDLER_H_
#define CEF_TESTS_CEFHEADLESS_HEADLESS_HANDLER_H_
#pragma once

#include <list>

#include "include/cef_app.h"
#include "include/cef_client.h"

// Client handler for headless Chrome runtime mode.
//
// Does NOT implement CefRenderHandler — Chrome runtime handles rendering
// internally via a virtual compositor surface in --headless mode.
// Screenshots are captured via CDP Page.captureScreenshot.
class HeadlessHandler : public CefClient,
                        public CefLifeSpanHandler,
                        public CefLoadHandler {
 public:
  HeadlessHandler(int width, int height, bool enable_stealth);
  ~HeadlessHandler() override;

  static HeadlessHandler* GetInstance();

  // CefClient methods:
  // No GetRenderHandler() — Chrome runtime doesn't use OSR.
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

  // CefLifeSpanHandler methods:
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefLoadHandler methods:
  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode errorCode,
                   const CefString& errorText,
                   const CefString& failedUrl) override;

  void CloseAllBrowsers(bool force_close);

 private:
  void ApplyStealth(CefRefPtr<CefBrowser> browser);

  const int width_;
  const int height_;
  const bool enable_stealth_;

  using BrowserList = std::list<CefRefPtr<CefBrowser>>;
  BrowserList browser_list_;

  IMPLEMENT_REFCOUNTING(HeadlessHandler);
};

#endif  // CEF_TESTS_CEFHEADLESS_HEADLESS_HANDLER_H_
