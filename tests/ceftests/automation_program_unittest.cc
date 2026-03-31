// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "include/cef_automation_program.h"
#include "tests/ceftests/test_handler.h"
#include "tests/ceftests/track_callback.h"
#include "tests/gtest/include/gtest/gtest.h"

namespace {

const char kAutomationProgramTestUrl[] = "https://tests/automation_program.html";

class AutomationProgramCallback : public CefAutomationProgramCallback {
 public:
  explicit AutomationProgramCallback(class AutomationProgramTestHandler* handler)
      : handler_(handler) {}

  void OnComplete(bool success,
                  CefRefPtr<CefListValue> results,
                  int failed_index,
                  const CefString& error) override;

 private:
  AutomationProgramTestHandler* const handler_;

  IMPLEMENT_REFCOUNTING(AutomationProgramCallback);
};

class AutomationProgramTestHandler : public TestHandler {
 public:
  AutomationProgramTestHandler() = default;

  void RunTest() override {
    AddResource(kAutomationProgramTestUrl, "<html><body>Automation</body></html>",
                "text/html");
    CreateBrowser(kAutomationProgramTestUrl);
    SetTestTimeout();
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int httpStatusCode) override {
    if (!frame->IsMain() || got_callback_) {
      return;
    }

    auto program = CefAutomationProgram::Create();
    ASSERT_TRUE(program.get());

    auto navigate_params = CefDictionaryValue::Create();
    navigate_params->SetString("url", "https://example.test/");
    EXPECT_EQ(0, program->AddInstruction(CEF_AUTOMATION_INSTRUCTION_NAVIGATE,
                                         navigate_params));

    auto delay_params = CefDictionaryValue::Create();
    delay_params->SetInt("milliseconds", 25);
    EXPECT_EQ(1, program->AddInstruction(CEF_AUTOMATION_INSTRUCTION_DELAY,
                                         delay_params));

    browser->GetHost()->ExecuteAutomationProgram(
        program, new AutomationProgramCallback(this));
  }

  void OnProgramComplete(bool success,
                         CefRefPtr<CefListValue> results,
                         int failed_index,
                         const CefString& error) {
    EXPECT_UI_THREAD();
    EXPECT_FALSE(got_callback_);
    EXPECT_TRUE(success);
    ASSERT_TRUE(results.get());
    EXPECT_EQ(2u, results->GetSize());
    EXPECT_EQ(VTYPE_STRING, results->GetType(0));
    EXPECT_EQ(VTYPE_STRING, results->GetType(1));
    EXPECT_STREQ("pending", results->GetString(0).ToString().c_str());
    EXPECT_STREQ("pending", results->GetString(1).ToString().c_str());
    EXPECT_EQ(-1, failed_index);
    EXPECT_TRUE(error.empty());
    got_callback_.yes();
    DestroyTest();
  }

 private:
  void DestroyTest() override {
    EXPECT_TRUE(got_callback_);
    TestHandler::DestroyTest();
  }

  TrackCallback got_callback_;

  IMPLEMENT_REFCOUNTING(AutomationProgramTestHandler);
};

void AutomationProgramCallback::OnComplete(bool success,
                                           CefRefPtr<CefListValue> results,
                                           int failed_index,
                                           const CefString& error) {
  handler_->OnProgramComplete(success, results, failed_index, error);
}

}  // namespace

TEST(AutomationProgramTest, ExecutesViaBrowserHostScaffold) {
  CefRefPtr<AutomationProgramTestHandler> handler =
      new AutomationProgramTestHandler;
  handler->ExecuteTest();
  ReleaseAndWaitForDestructor(handler);
}
