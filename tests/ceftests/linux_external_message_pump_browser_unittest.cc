// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "include/cef_browser_capture.h"
#include "include/cef_parser.h"
#include "tests/ceftests/test_handler.h"
#include "tests/gtest/include/gtest/gtest.h"

namespace {

const char kPumpBrowserTestUrl[] = "https://tests/linux-external-pump.html";

class LinuxExternalPumpBrowserTestHandler;

class EvalSnapshotCallback : public CefEvalSnapshotCallback {
 public:
  explicit EvalSnapshotCallback(LinuxExternalPumpBrowserTestHandler* handler)
      : handler_(handler) {}

  void OnComplete(bool eval_success,
                  CefRefPtr<CefValue> eval_result,
                  const CefString& eval_error,
                  const CefString& snapshot) override;

 private:
  LinuxExternalPumpBrowserTestHandler* const handler_;
  IMPLEMENT_REFCOUNTING(EvalSnapshotCallback);
};

class ScreenshotCallback : public CefScreenshotCallback {
 public:
  explicit ScreenshotCallback(LinuxExternalPumpBrowserTestHandler* handler)
      : handler_(handler) {}

  void OnScreenshotCaptured(const CefString& path,
                            const CefString& error) override;

 private:
  LinuxExternalPumpBrowserTestHandler* const handler_;
  IMPLEMENT_REFCOUNTING(ScreenshotCallback);
};

class LinuxExternalPumpBrowserTestHandler : public TestHandler {
 public:
  void RunTest() override {
    AddResource(
        kPumpBrowserTestUrl,
        "<html><body>"
        "<button id='go'>Go</button>"
        "<input id='text' value='hello' />"
        "<a id='link' href='#target'>Link</a>"
        "<script>"
        "window.__pumpCounter = 0;"
        "setInterval(() => { window.__pumpCounter++; }, 10);"
        "</script>"
        "</body></html>",
        "text/html");
    CreateBrowser(kPumpBrowserTestUrl);
    SetTestTimeout();
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int http_status_code) override {
    if (!frame->IsMain() || started_) {
      return;
    }
    started_ = true;

    CefRefPtr<CefBrowserCapture> capture = browser->GetHost()->GetCapture();
    ASSERT_TRUE(capture);

    CefSnapshotSettings snapshot_settings;
    snapshot_settings.interactive_only = 1;
    snapshot_settings.compact = 1;

    capture->EvalThenSnapshot(
        "(() => ({ counter: window.__pumpCounter, title: document.title }))()",
        snapshot_settings, new EvalSnapshotCallback(this));
  }

  void OnEvalSnapshotComplete(bool eval_success,
                              CefRefPtr<CefValue> eval_result,
                              const CefString& eval_error,
                              const CefString& snapshot) {
    EXPECT_UI_THREAD();
    EXPECT_TRUE(eval_success) << eval_error.ToString();
    ASSERT_TRUE(eval_result);
    ASSERT_EQ(VTYPE_DICTIONARY, eval_result->GetType());
    EXPECT_FALSE(snapshot.empty());
    EXPECT_EQ(0, snapshot.ToString().find("Browser capture snapshot scaffolding"));

    CefAnnotatedScreenshotSettings screenshot_settings;
    screenshot_settings.annotate = 1;
    screenshot_settings.quality = 80;
    screenshot_settings.format = "png";

    CefRefPtr<CefBrowserCapture> capture = GetBrowser()->GetHost()->GetCapture();
    capture->CaptureAnnotatedScreenshot("linux-external-pump-browser.png",
                                        screenshot_settings,
                                        new ScreenshotCallback(this));
  }

  void OnScreenshotComplete(const CefString& path, const CefString& error) {
    EXPECT_UI_THREAD();
    EXPECT_STREQ("linux-external-pump-browser.png", path.ToString().c_str());
    EXPECT_TRUE(error.empty()) << error.ToString();
    DestroyTest();
  }

 private:
  bool started_ = false;

  IMPLEMENT_REFCOUNTING(LinuxExternalPumpBrowserTestHandler);
};

void EvalSnapshotCallback::OnComplete(bool eval_success,
                                      CefRefPtr<CefValue> eval_result,
                                      const CefString& eval_error,
                                      const CefString& snapshot) {
  handler_->OnEvalSnapshotComplete(eval_success, eval_result, eval_error,
                                   snapshot);
}

void ScreenshotCallback::OnScreenshotCaptured(const CefString& path,
                                              const CefString& error) {
  handler_->OnScreenshotComplete(path, error);
}

}  // namespace

TEST(LinuxExternalMessagePumpBrowserTest, CaptureAndEvalPipeline) {
  CefRefPtr<LinuxExternalPumpBrowserTestHandler> handler =
      new LinuxExternalPumpBrowserTestHandler();
  handler->ExecuteTest();
  ReleaseAndWaitForDestructor(handler);
}
