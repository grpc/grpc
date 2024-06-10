#include <asm-generic/errno.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstring>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_split.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>

#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/event_poller_posix_default.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "test/core/event_engine/posix/posix_engine_test_utils.h"
#include "test/core/test_util/port.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

TEST(PollerForkTest, ReleasesEventHandleInChild) {
  EventHandleRef ref;
  // int port = grpc_pick_unused_port_or_die();
  // Does not really matter, we just need fd
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  EXPECT_NE(fd, 0) << std::strerror(errno);
  auto cleanup = absl::MakeCleanup([fd]() { close(fd); });
  auto scheduler =
      std::make_unique<grpc_event_engine::experimental::TestScheduler>(nullptr);
  EXPECT_NE(scheduler, nullptr);
  auto poller = MakeDefaultPoller(scheduler.get());
  ref = poller->CreateHandle(fd, "Test handle", false);
  EXPECT_NE(ref.get(), nullptr);
  int pid = fork();
  if (pid < 0) {
    FAIL() << "Fork failed";
  } else if (pid == 0) {  // Child
    // Disabled for now
    // EXPECT_EQ(ref.get(), nullptr);
  } else {  // parent
    EXPECT_NE(ref.get(), nullptr);
    int status;
    wait(&status);
    EXPECT_EQ(status, 0);
  }
  // auto posix_ee = PosixEventEngine::MakeTestOnlyPosixEventEngine(poller);
}

}  // namespace
}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  auto poll_strategy = grpc_core::ConfigVars::Get().PollStrategy();
  auto strings = absl::StrSplit(poll_strategy, ',');
  if (std::find(strings.begin(), strings.end(), "none") != strings.end()) {
    // Skip the test entirely if poll strategy is none.
    return 0;
  }
  // TODO(ctiller): EventEngine temporarily needs grpc to be initialized first
  // until we clear out the iomgr shutdown code.
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}