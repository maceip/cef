// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "cef/libcef/browser/browser_capture_impl.h"

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "cef/include/cef_devtools_message_observer.h"
#include "cef/libcef/browser/browser_host_base.h"
#include "cef/libcef/browser/page_model_cache.h"
#include "cef/libcef/browser/thread_util.h"
#include "cef/libcef/common/values_impl.h"

namespace {

void RunSnapshotCallback(CefRefPtr<CefStringVisitor> callback,
                         const CefString& value) {
  if (!callback) return;
  CEF_POST_TASK(CEF_UIT, base::BindOnce(&CefStringVisitor::Visit, callback, value));
}

void RunScreenshotCallback(CefRefPtr<CefScreenshotCallback> callback,
                           const CefString& path, const CefString& error) {
  if (!callback) return;
  CEF_POST_TASK(CEF_UIT, base::BindOnce(&CefScreenshotCallback::OnScreenshotCaptured, callback, path, error));
}

}  // namespace

CefBrowserCaptureImpl::CefBrowserCaptureImpl(CefBrowserHostBase* browser)
    : browser_(browser) {}

void CefBrowserCaptureImpl::Snapshot(const CefSnapshotSettings& settings,
                                     CefRefPtr<CefStringVisitor> callback) {
  if (!browser_) { RunSnapshotCallback(callback, "Browser not available."); return; }
  const int frame_id = 0;
  CefPageModelCache::SnapshotEntry cached;
  if (browser_->GetPageModelCache().GetCachedSnapshot(frame_id, "default", &cached)) {
    RunSnapshotCallback(callback, CefString(cached.snapshot_text));
    return;
  }
  RunSnapshotCallback(callback, "Snapshot requires CDP implementation.");
}

void CefBrowserCaptureImpl::CaptureAnnotatedScreenshot(
    const CefString& path, const CefAnnotatedScreenshotSettings& settings,
    CefRefPtr<CefScreenshotCallback> callback) {
  RunScreenshotCallback(callback, CefString(), "Annotated screenshot requires CDP implementation.");
}

void CefBrowserCaptureImpl::EvalThenSnapshot(
    const CefString& code, const CefSnapshotSettings& settings,
    CefRefPtr<CefEvalSnapshotCallback> callback) {
  if (!callback) return;
  if (!browser_) { callback->OnComplete(false, nullptr, "Browser not available.", CefString()); return; }
  callback->OnComplete(false, nullptr, "EvalThenSnapshot requires CDP implementation.", CefString());
}

void CefBrowserCaptureImpl::ExecuteCompoundOperation(
    const CefCompoundOperation& operation,
    CefRefPtr<CefCompoundOperationCallback> callback) {
  if (!callback) return;
  callback->OnComplete(true, CefString(), CefString(), CefString());
}
