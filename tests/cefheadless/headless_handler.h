// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#ifndef CEF_TESTS_CEFHEADLESS_HEADLESS_HANDLER_H_
#define CEF_TESTS_CEFHEADLESS_HEADLESS_HANDLER_H_
#pragma once

#include <list>

#include "include/cef_client.h"

class HeadlessHandler : public CefClient,
                        public CefLifeSpanHandler,
                        public CefLoadHandler,
                        public CefRenderHandler {
 public:
  HeadlessHandler(int width, int height, bool enable_stealth);
  ~HeadlessHandler() override;

  static HeadlessHandler* GetInstance();

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode errorCode,
                   const CefString& errorText,
                   const CefString& failedUrl) override;

  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
  void OnPaint(CefRefPtr<CefBrowser> browser,
               PaintElementType type,
               const RectList& dirtyRects,
               const void* buffer,
               int width,
               int height) override;

  void CloseAllBrowsers(bool force_close);

 private:
  void ApplyStealthConfig(CefRefPtr<CefBrowser> browser);

  const int width_;
  const int height_;
  const bool enable_stealth_;

  using BrowserList = std::list<CefRefPtr<CefBrowser>>;
  BrowserList browser_list_;
  bool is_closing_ = false;

  IMPLEMENT_REFCOUNTING(HeadlessHandler);
};

#endif  // CEF_TESTS_CEFHEADLESS_HEADLESS_HANDLER_H_
