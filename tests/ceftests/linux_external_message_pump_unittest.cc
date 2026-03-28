#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_waitable_event.h"
#include "include/wrapper/cef_linux_external_message_pump.h"
#include "tests/gtest/include/gtest/gtest.h"

#if defined(OS_LINUX)

#include <atomic>
#include <thread>

namespace {

constexpr int kDefaultWaitMs = 2000;

class PumpThreadRunner {
 public:
  explicit PumpThreadRunner(
      std::unique_ptr<CefLinuxExternalMessagePump> pump)
      : pump_(std::move(pump)) {}

  void Start() {
    thread_ = std::thread([this]() { exit_code_ = pump_->Run(); });
  }

  void Stop() {
    pump_->Quit();
    Join();
  }

  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  CefLinuxExternalMessagePump* pump() const { return pump_.get(); }
  int exit_code() const { return exit_code_; }

 private:
  std::unique_ptr<CefLinuxExternalMessagePump> pump_;
  std::thread thread_;
  int exit_code_ = -1;
};

}  // namespace

TEST(LinuxExternalMessagePumpTest, CoalescesLessUrgentDelayedWork) {
  std::atomic<int> work_count{0};

  auto pump = CefLinuxExternalMessagePump::CreateForTest(
      base::BindRepeating(
          [](std::atomic<int>* counter) { counter->fetch_add(1); }, &work_count));

  pump->OnScheduleMessagePumpWork(100);
  pump->OnScheduleMessagePumpWork(150);

  auto stats = pump->GetStats();
  EXPECT_EQ(2u, stats.schedule_requests);
  EXPECT_EQ(1u, stats.wakeup_coalesces);
  EXPECT_TRUE(pump->IsTimerPending());
}

TEST(LinuxExternalMessagePumpTest, EarlierDelayOverridesLaterOne) {
  auto pump = CefLinuxExternalMessagePump::CreateForTest(
      base::BindRepeating([]() {}));

  pump->OnScheduleMessagePumpWork(250);
  pump->OnScheduleMessagePumpWork(10);

  const auto stats = pump->GetStats();
  EXPECT_EQ(2u, stats.schedule_requests);
  EXPECT_EQ(0u, stats.wakeup_coalesces);
}

TEST(LinuxExternalMessagePumpTest, RunsImmediateWorkAndTracksStats) {
  CefRefPtr<CefWaitableEvent> ran_event =
      CefWaitableEvent::CreateWaitableEvent(true, false);
  std::atomic<int> work_count{0};

  auto pump = CefLinuxExternalMessagePump::CreateForTest(base::BindRepeating(
      [](std::atomic<int>* counter, CefRefPtr<CefWaitableEvent> event) {
        counter->fetch_add(1);
        event->Signal();
      },
      &work_count, ran_event));

  PumpThreadRunner runner(std::move(pump));
  runner.Start();

  runner.pump()->OnScheduleMessagePumpWork(0);
  EXPECT_TRUE(ran_event->TimedWait(kDefaultWaitMs));

  runner.Stop();
  EXPECT_EQ(0, runner.exit_code());
  EXPECT_EQ(1, work_count.load());

  const auto stats = runner.pump()->GetStats();
  EXPECT_EQ(1u, stats.schedule_requests);
  EXPECT_EQ(1u, stats.immediate_requests);
  EXPECT_EQ(1u, stats.do_work_runs);
}

TEST(LinuxExternalMessagePumpTest, DelayedWorkDoesNotRunTooEarly) {
  CefRefPtr<CefWaitableEvent> ran_event =
      CefWaitableEvent::CreateWaitableEvent(true, false);
  std::atomic<int> work_count{0};

  auto pump = CefLinuxExternalMessagePump::CreateForTest(base::BindRepeating(
      [](std::atomic<int>* counter, CefRefPtr<CefWaitableEvent> event) {
        counter->fetch_add(1);
        event->Signal();
      },
      &work_count, ran_event));

  PumpThreadRunner runner(std::move(pump));
  runner.Start();

  runner.pump()->OnScheduleMessagePumpWork(200);

  EXPECT_FALSE(ran_event->TimedWait(50));
  EXPECT_TRUE(ran_event->TimedWait(kDefaultWaitMs));

  runner.Stop();
  EXPECT_EQ(1, work_count.load());

  const auto stats = runner.pump()->GetStats();
  EXPECT_EQ(1u, stats.delayed_requests);
  EXPECT_EQ(1u, stats.do_work_runs);
}

TEST(LinuxExternalMessagePumpTest, QuitIsDeterministic) {
  auto pump = CefLinuxExternalMessagePump::CreateForTest(base::BindRepeating([]() {
    // Intentionally empty.
  }));

  PumpThreadRunner runner(std::move(pump));
  runner.Start();
  runner.Stop();

  EXPECT_EQ(0, runner.exit_code());
  const auto stats = runner.pump()->GetStats();
  EXPECT_GE(stats.quit_latency_ms, 0);
}

#endif  // defined(OS_LINUX)
