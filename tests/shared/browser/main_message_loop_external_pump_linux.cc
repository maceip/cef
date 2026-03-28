// Copyright (c) 2016 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "tests/shared/browser/main_message_loop_external_pump.h"

#include <errno.h>
#include <glib.h>

#include <memory>

#include "include/base/cef_logging.h"
#include "include/wrapper/cef_linux_external_message_pump.h"

namespace client {

namespace {

class MainMessageLoopExternalPumpLinux : public MainMessageLoopExternalPump {
 public:
  MainMessageLoopExternalPumpLinux();
  ~MainMessageLoopExternalPumpLinux();

  // MainMessageLoopStd methods:
  void Quit() override;
  int Run() override;

  // MainMessageLoopExternalPump methods:
  void OnScheduleMessagePumpWork(int64_t delay_ms) override;

  // Internal methods used for processing the pump callbacks. They are public
  // for simplicity but should not be used directly. HandlePrepare is called
  // during the prepare step of glib, and returns a timeout that will be passed
  // to the poll. HandleCheck is called after the poll has completed, and
  // returns whether or not HandleDispatch should be called. HandleDispatch is
  // called if HandleCheck returned true.
  int HandlePrepare();
  bool HandleCheck();
  void HandleDispatch();

 private:
  // Used to flag that the Run() invocation should return ASAP.
  bool should_quit_;

  // A GLib structure that we can add event sources to. We use the default GLib
  // context, which is the one to which all GTK events are dispatched.
  GMainContext* context_;

  // The work source. It is destroyed when the message pump is destroyed.
  GSource* work_source_;

  std::unique_ptr<CefLinuxExternalMessagePump> pump_;
  std::unique_ptr<GPollFD> wakeup_gpollfd_;
};

struct WorkSource : public GSource {
  MainMessageLoopExternalPumpLinux* pump;
};

gboolean WorkSourcePrepare(GSource* source, gint* timeout_ms) {
  *timeout_ms = static_cast<WorkSource*>(source)->pump->HandlePrepare();
  // We always return FALSE, so that our timeout is honored.  If we were
  // to return TRUE, the timeout would be considered to be 0 and the poll
  // would never block.  Once the poll is finished, Check will be called.
  return FALSE;
}

gboolean WorkSourceCheck(GSource* source) {
  // Only return TRUE if Dispatch should be called.
  return static_cast<WorkSource*>(source)->pump->HandleCheck();
}

gboolean WorkSourceDispatch(GSource* source,
                            GSourceFunc unused_func,
                            gpointer unused_data) {
  static_cast<WorkSource*>(source)->pump->HandleDispatch();
  // Always return TRUE so our source stays registered.
  return TRUE;
}

// I wish these could be const, but g_source_new wants non-const.
GSourceFuncs WorkSourceFuncs = {WorkSourcePrepare, WorkSourceCheck,
                                WorkSourceDispatch, nullptr};

MainMessageLoopExternalPumpLinux::MainMessageLoopExternalPumpLinux()
    : should_quit_(false),
      context_(g_main_context_default()),
      wakeup_gpollfd_(new GPollFD) {
  pump_ = CefLinuxExternalMessagePump::Create();
  wakeup_gpollfd_->fd = pump_->GetWakeupReadFd();
  wakeup_gpollfd_->events = G_IO_IN;

  work_source_ = g_source_new(&WorkSourceFuncs, sizeof(WorkSource));
  static_cast<WorkSource*>(work_source_)->pump = this;
  g_source_add_poll(work_source_, wakeup_gpollfd_.get());
  // Use a low priority so that we let other events in the queue go first.
  g_source_set_priority(work_source_, G_PRIORITY_DEFAULT_IDLE);
  // This is needed to allow Run calls inside Dispatch.
  g_source_set_can_recurse(work_source_, TRUE);
  g_source_attach(work_source_, context_);
}

MainMessageLoopExternalPumpLinux::~MainMessageLoopExternalPumpLinux() {
  g_source_destroy(work_source_);
  g_source_unref(work_source_);
}

void MainMessageLoopExternalPumpLinux::Quit() {
  should_quit_ = true;
  pump_->RequestQuit();
}

int MainMessageLoopExternalPumpLinux::Run() {
  // We really only do a single task for each iteration of the loop. If we
  // have done something, assume there is likely something more to do. This
  // will mean that we don't block on the message pump until there was nothing
  // more to do. We also set this to true to make sure not to block on the
  // first iteration of the loop.
  bool more_work_is_plausible = true;

  // We run our own loop instead of using g_main_loop_quit in one of the
  // callbacks. This is so we only quit our own loops, and we don't quit
  // nested loops run by others.
  for (;;) {
    // Don't block if we think we have more work to do.
    bool block = !more_work_is_plausible;

    more_work_is_plausible = g_main_context_iteration(context_, block);
    if (should_quit_) {
      break;
    }
  }

  return 0;
}

void MainMessageLoopExternalPumpLinux::OnScheduleMessagePumpWork(
    int64_t delay_ms) {
  pump_->OnScheduleMessagePumpWork(delay_ms);
}

// Return the timeout we want passed to poll.
int MainMessageLoopExternalPumpLinux::HandlePrepare() {
  return pump_->GetPollTimeoutMs();
}

bool MainMessageLoopExternalPumpLinux::HandleCheck() {
  if (wakeup_gpollfd_->revents & G_IO_IN) {
    pump_->DrainWakeups();
  }
  return pump_->ShouldDispatchNow();
}

void MainMessageLoopExternalPumpLinux::HandleDispatch() {
  pump_->PerformWork();
}

}  // namespace

// static
std::unique_ptr<MainMessageLoopExternalPump>
MainMessageLoopExternalPump::Create() {
  return std::make_unique<MainMessageLoopExternalPumpLinux>();
}

}  // namespace client
