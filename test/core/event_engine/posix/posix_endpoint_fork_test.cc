#include <sched.h>

#include <cstdlib>
#include <string>
#include <thread>

#include "absl/log/log.h"
#include "absl/strings/str_split.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>

#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/util/subprocess.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

class ForkClient {
 public:
  ForkClient(const std::string& message) : message_(message) {
    const char* argv[] = {"test/core/event_engine/posix/fork_executable"};
    subprocess_ = gpr_subprocess_create_with_envp(1, argv, 0, {});
    if (subprocess_ != nullptr) {
      background_ = std::thread(CommunicateWithChild, this);
    }
  }

  ~ForkClient() { background_.join(); }

  bool valid() const { return subprocess_ != nullptr; }

  std::pair<std::string, std::string> GetChildOutputs() {
    grpc_core::MutexLock lock(&mu_);
    while (!done_) {
      cond_.Wait(&mu_);
    }
    return {output_, error_};
  }

 private:
  static void CommunicateWithChild(ForkClient* client) {
    grpc_core::MutexLock lock(&client->mu_);
    gpr_subprocess_communicate(client->subprocess_, client->message_,
                               &client->output_, &client->error_);
    client->done_ = true;
    client->cond_.SignalAll();
  }

  gpr_subprocess* subprocess_;
  std::string message_ ABSL_GUARDED_BY(&mu_);
  std::string output_ ABSL_GUARDED_BY(&mu_);
  std::string error_ ABSL_GUARDED_BY(&mu_);
  bool done_ ABSL_GUARDED_BY(&mu_) = false;
  std::thread background_;
  grpc_core::Mutex mu_;
  grpc_core::CondVar cond_;
};

TEST(EndpointForkTest, ConnectAndFork) {
  ForkClient client{"boop"};
  ASSERT_TRUE(client.valid());
  auto output = client.GetChildOutputs();
  EXPECT_EQ(output.second, "");
  LOG(INFO) << output.first;
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
