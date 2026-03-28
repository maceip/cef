#include "include/wrapper/cef_linux_external_message_pump.h"

#if defined(OS_LINUX)

#include <errno.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>

#include "base/strings/stringprintf.h"
#include "base/synchronization/lock.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "include/base/cef_logging.h"
#include "include/cef_app.h"

namespace {

constexpr int64_t kMaxPollIntervalMs = 1000 / 30;  // 30fps safety cap.

#if !defined(HANDLE_EINTR)
#if !DCHECK_IS_ON()
#define HANDLE_EINTR(x)                                     \
  ({                                                        \
    decltype(x) eintr_wrapper_result;                       \
    do {                                                    \
      eintr_wrapper_result = (x);                           \
    } while (eintr_wrapper_result == -1 && errno == EINTR); \
    eintr_wrapper_result;                                   \
  })
#else
#define HANDLE_EINTR(x)                                      \
  ({                                                         \
    int eintr_wrapper_counter = 0;                           \
    decltype(x) eintr_wrapper_result;                        \
    do {                                                     \
      eintr_wrapper_result = (x);                            \
    } while (eintr_wrapper_result == -1 && errno == EINTR && \
             eintr_wrapper_counter++ < 100);                 \
    eintr_wrapper_result;                                    \
  })
#endif
#endif

std::string FormatStats(const CefLinuxExternalMessagePump::Stats& stats) {
  return base::StringPrintf(
      "scheduled=%" PRIu64 " immediate=%" PRIu64
      " delayed=%" PRIu64 " work_runs=%" PRIu64
      " wakeups=%" PRIu64 " coalesced=%" PRIu64
      " reentrancy_reposts=%" PRIu64
      " idle_sleeps=%" PRIu64 " max_idle_sleeps=%" PRIu64
      " max_lag_ms=%" PRId64 " quit_latency_ms=%" PRId64,
      stats.schedule_requests, stats.immediate_requests,
      stats.delayed_requests, stats.do_work_runs, stats.wakeups,
      stats.wakeup_coalesces, stats.reentrancy_reposts, stats.idle_sleeps,
      stats.max_consecutive_idle_sleeps, stats.max_schedule_lag_ms,
      stats.quit_latency_ms);
}

base::RepeatingClosure MakeDefaultDoWorkClosure() {
  return base::BindRepeating([]() { CefDoMessageLoopWork(); });
}

}  // namespace

class CefLinuxExternalMessagePump::Impl {
 public:
  explicit Impl(const base::RepeatingClosure& do_work)
      : do_work_(do_work),
        owner_thread_id_(base::PlatformThread::CurrentId()),
        wakeup_fd_(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {
    DCHECK(do_work_);
    PCHECK(wakeup_fd_ >= 0);
  }

  ~Impl() {
    if (wakeup_fd_ >= 0) {
      close(wakeup_fd_);
    }
  }

  int Run() {
    DCHECK_EQ(owner_thread_id_, base::PlatformThread::CurrentId());

    quit_requested_ = false;
    {
      base::AutoLock lock(lock_);
      stats_ = Stats();
      next_due_time_ = base::TimeTicks();
      quit_start_time_ = base::TimeTicks();
      schedule_pending_ = false;
      consecutive_idle_sleeps_ = 0;
    }

    for (;;) {
      const int timeout_ms = GetPollTimeoutMs();
      struct pollfd fd = {wakeup_fd_, POLLIN, 0};
      const int poll_result = HANDLE_EINTR(poll(&fd, 1, timeout_ms));
      if (poll_result < 0) {
        PLOG(ERROR) << "poll failed";
        return 1;
      }

      if (fd.revents & POLLIN) {
        DrainWakeups();
      }

      if (quit_requested_) {
        FinalizeQuitLatency();
        return 0;
      }

      if (!PerformWork()) {
        base::AutoLock lock(lock_);
        stats_.idle_sleeps++;
        consecutive_idle_sleeps_++;
        stats_.max_consecutive_idle_sleeps =
            std::max(stats_.max_consecutive_idle_sleeps,
                     consecutive_idle_sleeps_);
      }
    }
  }

  void Quit() {
    if (!quit_requested_) {
      quit_requested_ = true;
      {
        base::AutoLock lock(lock_);
        if (quit_start_time_.is_null()) {
          quit_start_time_ = base::TimeTicks::Now();
        }
      }
      SignalWake();
    }
  }

  void OnScheduleMessagePumpWork(int64_t delay_ms) {
    const auto now = base::TimeTicks::Now();
    const int64_t due_delay_ms = std::max<int64_t>(0, delay_ms);
    const auto due_time = now + base::Milliseconds(due_delay_ms);
    bool should_signal = true;

    {
      base::AutoLock lock(lock_);
      stats_.schedule_requests++;
      stats_.last_requested_delay_ms = delay_ms;
      stats_.last_schedule_time_us = now.since_origin().InMicroseconds();
      if (due_delay_ms == 0) {
        stats_.immediate_requests++;
      } else {
        stats_.delayed_requests++;
      }

      if (schedule_pending_ && !next_due_time_.is_null() &&
          due_time >= next_due_time_) {
        stats_.wakeup_coalesces++;
        should_signal = false;
      } else {
        next_due_time_ = due_time;
        schedule_pending_ = true;
      }
    }

    if (debug_logging_enabled_) {
      LOG(INFO) << "linux-external-pump schedule delay_ms=" << delay_ms
                << " summary=" << FormatStats(GetStats());
    }

    if (should_signal) {
      SignalWake();
    }
  }

  int GetWakeupReadFd() const { return wakeup_fd_; }

  int GetPollTimeoutMs() const {
    base::AutoLock lock(lock_);
    if (!schedule_pending_) {
      return -1;
    }

    const auto now = base::TimeTicks::Now();
    if (next_due_time_.is_null() || next_due_time_ <= now) {
      return 0;
    }

    return static_cast<int>(std::min<int64_t>(
        (next_due_time_ - now).InMillisecondsRoundedUp(), kMaxPollIntervalMs));
  }

  void DrainWakeups() {
    uint64_t value = 0;
    while (HANDLE_EINTR(read(wakeup_fd_, &value, sizeof(value))) ==
           sizeof(value)) {
    }

    base::AutoLock lock(lock_);
    stats_.wakeups++;
  }

  bool ShouldDispatchNow() const {
    base::AutoLock lock(lock_);
    if (!schedule_pending_) {
      return false;
    }
    const auto now = base::TimeTicks::Now();
    return next_due_time_.is_null() || next_due_time_ <= now;
  }

  bool PerformWork() {
    DCHECK_EQ(owner_thread_id_, base::PlatformThread::CurrentId());

    base::TimeTicks scheduled_time;
    {
      base::AutoLock lock(lock_);
      if (!schedule_pending_) {
        return false;
      }

      const auto now = base::TimeTicks::Now();
      if (!next_due_time_.is_null() && now < next_due_time_) {
        return false;
      }

      scheduled_time = next_due_time_.is_null() ? now : next_due_time_;
      schedule_pending_ = false;
      next_due_time_ = base::TimeTicks();
      consecutive_idle_sleeps_ = 0;
    }

    if (is_active_) {
      base::AutoLock lock(lock_);
      stats_.reentrancy_reposts++;
      schedule_pending_ = true;
      next_due_time_ = base::TimeTicks::Now();
      SignalWake();
      return false;
    }

    is_active_ = true;
    const auto start = base::TimeTicks::Now();
    do_work_.Run();
    const auto end = base::TimeTicks::Now();
    is_active_ = false;

    {
      base::AutoLock lock(lock_);
      stats_.do_work_runs++;
      stats_.last_work_time_us = end.since_origin().InMicroseconds();
      stats_.max_schedule_lag_ms = std::max<int64_t>(
          stats_.max_schedule_lag_ms,
          (start - scheduled_time).InMilliseconds());
    }

    if (debug_logging_enabled_) {
      LOG(INFO) << "linux-external-pump work summary="
                << FormatStats(GetStats());
    }

    return true;
  }

  bool IsTimerPending() const {
    base::AutoLock lock(lock_);
    return schedule_pending_ && !next_due_time_.is_null();
  }

  Stats GetStats() const {
    base::AutoLock lock(lock_);
    return stats_;
  }

  CefString GetDebugSummary() const { return FormatStats(GetStats()); }

  void SetDebugLoggingEnabled(bool enabled) {
    debug_logging_enabled_ = enabled;
  }

 private:
  void SignalWake() const {
    const uint64_t value = 1;
    if (HANDLE_EINTR(write(wakeup_fd_, &value, sizeof(value))) !=
        sizeof(value)) {
      if (errno != EAGAIN) {
        PLOG(ERROR) << "Failed to signal Linux external message pump";
      }
    }
  }

  void FinalizeQuitLatency() {
    base::AutoLock lock(lock_);
    if (!quit_start_time_.is_null()) {
      stats_.quit_latency_ms =
          (base::TimeTicks::Now() - quit_start_time_).InMilliseconds();
    }
  }

  const base::RepeatingClosure do_work_;
  const base::PlatformThreadId owner_thread_id_;
  const int wakeup_fd_;

  mutable base::Lock lock_;
  Stats stats_;
  base::TimeTicks next_due_time_;
  base::TimeTicks quit_start_time_;
  bool schedule_pending_ = false;
  uint64_t consecutive_idle_sleeps_ = 0;

  bool quit_requested_ = false;
  bool is_active_ = false;
  bool debug_logging_enabled_ = false;
};

namespace {
CefLinuxExternalMessagePump* g_active_pump = nullptr;
}  // namespace

// static
std::unique_ptr<CefLinuxExternalMessagePump> CefLinuxExternalMessagePump::Create() {
  return std::unique_ptr<CefLinuxExternalMessagePump>(
      new CefLinuxExternalMessagePump(
          std::make_unique<Impl>(MakeDefaultDoWorkClosure())));
}

// static
std::unique_ptr<CefLinuxExternalMessagePump>
CefLinuxExternalMessagePump::CreateForTest(
    const base::RepeatingClosure& do_work) {
  return std::unique_ptr<CefLinuxExternalMessagePump>(
      new CefLinuxExternalMessagePump(std::make_unique<Impl>(do_work)));
}

// static
CefLinuxExternalMessagePump* CefLinuxExternalMessagePump::GetActive() {
  return g_active_pump;
}

CefLinuxExternalMessagePump::CefLinuxExternalMessagePump(
    std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {
  DCHECK(!g_active_pump);
  g_active_pump = this;
}

CefLinuxExternalMessagePump::~CefLinuxExternalMessagePump() {
  if (g_active_pump == this) {
    g_active_pump = nullptr;
  }
}

int CefLinuxExternalMessagePump::Run() {
  return impl_->Run();
}

void CefLinuxExternalMessagePump::Quit() {
  impl_->Quit();
}

void CefLinuxExternalMessagePump::RequestQuit() {
  impl_->Quit();
}

void CefLinuxExternalMessagePump::OnScheduleMessagePumpWork(int64_t delay_ms) {
  impl_->OnScheduleMessagePumpWork(delay_ms);
}

int CefLinuxExternalMessagePump::GetWakeupReadFd() const {
  return impl_->GetWakeupReadFd();
}

int CefLinuxExternalMessagePump::GetPollTimeoutMs() const {
  return impl_->GetPollTimeoutMs();
}

void CefLinuxExternalMessagePump::DrainWakeups() {
  impl_->DrainWakeups();
}

bool CefLinuxExternalMessagePump::ShouldDispatchNow() {
  return impl_->ShouldDispatchNow();
}

bool CefLinuxExternalMessagePump::PerformWork() {
  return impl_->PerformWork();
}

bool CefLinuxExternalMessagePump::IsTimerPending() const {
  return impl_->IsTimerPending();
}

CefLinuxExternalMessagePump::Stats CefLinuxExternalMessagePump::GetStats() const {
  return impl_->GetStats();
}

CefString CefLinuxExternalMessagePump::GetDebugSummary() const {
  return impl_->GetDebugSummary();
}

void CefLinuxExternalMessagePump::SetDebugLoggingEnabled(bool enabled) {
  impl_->SetDebugLoggingEnabled(enabled);
}

#endif  // defined(OS_LINUX)
