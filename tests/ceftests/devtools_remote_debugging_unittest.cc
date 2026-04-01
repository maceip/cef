// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "include/cef_parser.h"
#include "include/cef_urlrequest.h"
#include "tests/ceftests/test_handler.h"
#include "tests/ceftests/test_request.h"
#include "tests/ceftests/track_callback.h"
#include "tests/gtest/include/gtest/gtest.h"

namespace {

const char kTestUrl[] = "https://tests/DevToolsRemoteDebugging";
const char kRemoteDebuggingOrigin[] = "http://127.0.0.1:9334";

class DevToolsRemoteDebuggingTestHandler : public TestHandler {
 public:
  DevToolsRemoteDebuggingTestHandler() = default;

  DevToolsRemoteDebuggingTestHandler(
      const DevToolsRemoteDebuggingTestHandler&) = delete;
  DevToolsRemoteDebuggingTestHandler& operator=(
      const DevToolsRemoteDebuggingTestHandler&) = delete;

  void RunTest() override {
    AddResource(kTestUrl, "<html><body>DevTools Remote Debugging</body></html>",
                "text/html");
    CreateBrowser(kTestUrl);
    SetTestTimeout();
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int httpStatusCode) override {
    if (!frame->IsMain() || version_requested_) {
      return;
    }

    EXPECT_EQ(200, httpStatusCode);
    version_requested_ = true;
    RequestJson(
        std::string(kRemoteDebuggingOrigin) + "/json/version",
        base::BindOnce(
            &DevToolsRemoteDebuggingTestHandler::OnVersionResponse, this));
  }

  void DestroyTest() override {
    EXPECT_TRUE(got_version_);
    EXPECT_TRUE(got_target_list_);
    TestHandler::DestroyTest();
  }

 private:
  using RequestState = test_request::State;

  void RequestJson(const std::string& url,
                   test_request::RequestDoneCallback callback) {
    test_request::SendConfig config;
    config.request_ = CefRequest::Create();
    config.request_->SetURL(url);
    config.request_->SetMethod("GET");
    test_request::Send(config, std::move(callback));
  }

  void OnVersionResponse(const RequestState& state) {
    EXPECT_UI_THREAD();
    EXPECT_FALSE(got_version_);
    got_version_.yes();

    EXPECT_EQ(UR_SUCCESS, state.status_);
    ASSERT_TRUE(state.response_);
    EXPECT_EQ(200, state.response_->GetStatus());

    CefRefPtr<CefValue> value =
        CefParseJSON(state.download_data_, JSON_PARSER_RFC);
    ASSERT_TRUE(value);
    ASSERT_EQ(VTYPE_DICTIONARY, value->GetType());

    CefRefPtr<CefDictionaryValue> dict = value->GetDictionary();
    ASSERT_TRUE(dict);
    EXPECT_TRUE(dict->HasKey("Browser"));
    EXPECT_TRUE(dict->HasKey("Protocol-Version"));
    EXPECT_TRUE(dict->HasKey("User-Agent"));
    EXPECT_FALSE(dict->GetString("Browser").ToString().empty());
    EXPECT_FALSE(dict->GetString("Protocol-Version").ToString().empty());
    EXPECT_FALSE(dict->GetString("User-Agent").ToString().empty());

    RequestJson(
        std::string(kRemoteDebuggingOrigin) + "/json/list",
        base::BindOnce(
            &DevToolsRemoteDebuggingTestHandler::OnTargetListResponse, this));
  }

  void OnTargetListResponse(const RequestState& state) {
    EXPECT_UI_THREAD();
    EXPECT_FALSE(got_target_list_);
    got_target_list_.yes();

    EXPECT_EQ(UR_SUCCESS, state.status_);
    ASSERT_TRUE(state.response_);
    EXPECT_EQ(200, state.response_->GetStatus());

    CefRefPtr<CefValue> value =
        CefParseJSON(state.download_data_, JSON_PARSER_RFC);
    ASSERT_TRUE(value);
    ASSERT_EQ(VTYPE_LIST, value->GetType());

    CefRefPtr<CefListValue> targets = value->GetList();
    ASSERT_TRUE(targets);
    EXPECT_GT(targets->GetSize(), 0u);

    bool found_target = false;
    for (size_t i = 0; i < targets->GetSize(); ++i) {
      if (targets->GetType(i) != VTYPE_DICTIONARY) {
        continue;
      }

      CefRefPtr<CefDictionaryValue> target = targets->GetDictionary(i);
      if (!target) {
        continue;
      }

      if (target->GetString("url").ToString() != kTestUrl) {
        continue;
      }

      found_target = true;
      EXPECT_EQ("page", target->GetString("type").ToString());
      const std::string ws_url =
          target->GetString("webSocketDebuggerUrl").ToString();
      EXPECT_FALSE(ws_url.empty());
      EXPECT_EQ(0u, ws_url.find("ws://127.0.0.1:9334/devtools/"));
      break;
    }

    EXPECT_TRUE(found_target);
    DestroyTest();
  }

  bool version_requested_ = false;
  TrackCallback got_version_;
  TrackCallback got_target_list_;

  IMPLEMENT_REFCOUNTING(DevToolsRemoteDebuggingTestHandler);
};

}  // namespace

TEST(DevToolsRemoteDebuggingTest, DiscoveryEndpoints) {
  CefRefPtr<DevToolsRemoteDebuggingTestHandler> handler =
      new DevToolsRemoteDebuggingTestHandler();
  handler->ExecuteTest();
  ReleaseAndWaitForDestructor(handler);
}
