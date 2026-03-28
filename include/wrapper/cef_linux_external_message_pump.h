// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#ifndef CEF_INCLUDE_WRAPPER_CEF_LINUX_EXTERNAL_MESSAGE_PUMP_H_
#define CEF_INCLUDE_WRAPPER_CEF_LINUX_EXTERNAL_MESSAGE_PUMP_H_
#pragma once

#include <stdint.h>

#include <memory>

#include "include/base/cef_build.h"
#include "include/base/cef_callback.h"
#include "include/cef_base.h"

#if defined(OS_LINUX)

///
/// Production Linux helper for external message pump integration.
///
/// This helper supports two usage patterns:
/// 1. Call Run() to let the helper own the outer loop for headless embedding.
/// 2. Integrate with an existing host loop by using GetWakeupReadFd(),
///    GetPollTimeoutMs(), DrainWakeups(), ShouldDispatchNow(), and PerformWork().
///
/// All CefDoMessageLoopWork() execution occurs on the thread that constructs and
/// runs this helper. Calls to OnScheduleMessagePumpWork() may come from any
/// thread.
///
class CefLinuxExternalMessagePump {
 public:
  struct Stats {
    uint64_t schedule_requests = 0;
    uint64_t immediate_requests = 0;
    uint64_t delayed_requests = 0;
    uint64_t do_work_runs = 0;
    uint64_t wakeups = 0;
    uint64_t wakeup_coalesces = 0;
    uint64_t timer_rearms = 0;
    uint64_t reentrancy_reposts = 0;
    uint64_t idle_sleeps = 0;
    uint64_t max_consecutive_idle_sleeps = 0;
    int64_t last_requested_delay_ms = 0;
    int64_t max_schedule_lag_ms = 0;
    int64_t quit_latency_ms = 0;
    int64_t last_schedule_time_us = 0;
    int64_t last_work_time_us = 0;
  };

  ///
  /// Creates a pump that executes CefDoMessageLoopWork().
  ///
  static std::unique_ptr<CefLinuxExternalMessagePump> Create();

  ///
  /// Creates a pump that executes |do_work| instead of CefDoMessageLoopWork().
  /// Intended for unit testing only.
  ///
  static std::unique_ptr<CefLinuxExternalMessagePump> CreateForTest(
      const base::RepeatingClosure& do_work);

  ///
  /// Returns the active pump instance if one exists on this process.
  ///
  static CefLinuxExternalMessagePump* GetActive();

  CefLinuxExternalMessagePump(const CefLinuxExternalMessagePump&) = delete;
  CefLinuxExternalMessagePump& operator=(const CefLinuxExternalMessagePump&) =
      delete;

  ~CefLinuxExternalMessagePump();

  ///
  /// Runs the owned outer loop until Quit() is called.
  ///
  int Run();

  ///
  /// Requests that the owned outer loop stop promptly.
  ///
  void Quit();

  ///
  /// Alias for Quit() when integrating with an existing host loop.
  ///
  void RequestQuit();

  ///
  /// Receives scheduling requests from
  /// CefBrowserProcessHandler::OnScheduleMessagePumpWork().
  ///
  void OnScheduleMessagePumpWork(int64_t delay_ms);

  ///
  /// Returns the Linux wakeup fd used for host-loop integration.
  ///
  int GetWakeupReadFd() const;

  ///
  /// Returns the timeout, in milliseconds, until the next required wakeup.
  /// Returns 0 for immediate work and -1 when no timeout is pending.
  ///
  int GetPollTimeoutMs() const;

  ///
  /// Drains the wakeup fd after it becomes readable.
  ///
  void DrainWakeups();

  ///
  /// Returns true when immediate or delayed work should run now.
  ///
  bool ShouldDispatchNow();

  ///
  /// Runs ready work. Returns true if message loop work executed.
  ///
  bool PerformWork();

  ///
  /// Returns true if a delayed timer is currently pending.
  ///
  bool IsTimerPending() const;

  ///
  /// Returns a snapshot of the current debug metrics.
  ///
  Stats GetStats() const;

  ///
  /// Returns a human-readable summary of the current debug metrics.
  ///
  CefString GetDebugSummary() const;

  ///
  /// Enables verbose per-event logging for schedule, work, and quit events.
  ///
  void SetDebugLoggingEnabled(bool enabled);

 private:
  class State;

  explicit CefLinuxExternalMessagePump(std::unique_ptr<State> state);

  std::unique_ptr<State> state_;
};

#endif  // defined(OS_LINUX)

#endif  // CEF_INCLUDE_WRAPPER_CEF_LINUX_EXTERNAL_MESSAGE_PUMP_H_
