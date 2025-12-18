// Copyright 2025 gRPC Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <dirent.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <stdio.h>
#include <sys/resource.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include "src/core/config/config_vars.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/notification.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/wait_for_single_owner.h"
#include "test/core/test_util/port.h"
#include "gtest/gtest.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

using Endpoint = ::grpc_event_engine::experimental::EventEngine::Endpoint;
using Listener = ::grpc_event_engine::experimental::EventEngine::Listener;
using namespace std::chrono_literals;

struct Connection {
  std::unique_ptr<EventEngine::Endpoint> client_endpoint;
  std::unique_ptr<EventEngine::Endpoint> server_endpoint;
};

Connection CreateConnectedEndpoints(std::shared_ptr<EventEngine> posix_ee) {
  auto memory_quota = std::make_unique<grpc_core::MemoryQuota>(
      grpc_core::MakeRefCounted<grpc_core::channelz::ResourceQuotaNode>("bar"));
  std::string target_addr = absl::StrCat(
      "ipv6:[::1]:", std::to_string(grpc_pick_unused_port_or_die()));
  auto resolved_addr = URIToResolvedAddress(target_addr);
  GRPC_CHECK_OK(resolved_addr);
  std::unique_ptr<EventEngine::Endpoint> client_endpoint;
  std::unique_ptr<EventEngine::Endpoint> server_endpoint;
  grpc_core::Notification client_signal;
  grpc_core::Notification server_signal;

  Listener::AcceptCallback accept_cb =
      [&server_endpoint, &server_signal](
          std::unique_ptr<Endpoint> ep,
          grpc_core::MemoryAllocator /*memory_allocator*/) {
        server_endpoint = std::move(ep);
        server_signal.Notify();
      };

  grpc_core::ChannelArgs args;
  auto quota = grpc_core::ResourceQuota::Default();
  args = args.Set(GRPC_ARG_RESOURCE_QUOTA, quota);
  ChannelArgsEndpointConfig config(args);
  auto listener = *posix_ee->CreateListener(
      std::move(accept_cb),
      [](absl::Status status) {
        ASSERT_TRUE(status.ok()) << status.ToString();
      },
      config,
      std::make_unique<grpc_core::MemoryQuota>(
          grpc_core::MakeRefCounted<grpc_core::channelz::ResourceQuotaNode>(
              "bar")));

  CHECK(listener->Bind(*resolved_addr).ok());
  CHECK(listener->Start().ok());

  posix_ee->Connect(
      [&client_endpoint,
       &client_signal](absl::StatusOr<std::unique_ptr<Endpoint>> endpoint) {
        ASSERT_TRUE(endpoint.ok());
        client_endpoint = std::move(*endpoint);
        client_signal.Notify();
      },
      *resolved_addr, config, memory_quota->CreateMemoryAllocator("conn-1"),
      24h);

  client_signal.WaitForNotification();
  server_signal.WaitForNotification();
  EXPECT_NE(client_endpoint.get(), nullptr);
  EXPECT_NE(server_endpoint.get(), nullptr);
  return Connection{std::move(client_endpoint), std::move(server_endpoint)};
}

int NumFilesUsed(int pid) {
  // count the number of lines in the file called filename
  std::string filename = absl::StrFormat("/proc/%d/fd", pid);
  DIR* dir;
  struct dirent* entry;
  int count = 0;

  // Open the directory
  dir = opendir(filename.c_str());
  if (dir == nullptr) {
    perror("Could not open directory");
    return -1;  // Return -1 to indicate an error
  }

  // Read all entries in the directory
  while ((entry = readdir(dir)) != nullptr) {
    count++;
  }

  // Close the directory
  closedir(dir);
  return count;
}

void WaitUntilNumFilesUsedDropsBelowThreshold(int pid, int threshold) {
  while (NumFilesUsed(pid) > threshold) {
    absl::SleepFor(absl::Milliseconds(100));
  }
}

}  // namespace

TEST(PosixEndpointSmokeTest, FdConservationTest) {
  struct rlimit rlim;
  struct rlimit prev_limit;
  // set max # of file descriptors to a low value, and
  // verify we can create and destroy many more than this number
  // of descriptors
  int pid = getpid();
  int threshold = NumFilesUsed(pid);
  // Reserve only 10 more fds.
  rlim.rlim_cur = rlim.rlim_max = threshold + 10;
  EXPECT_EQ(prlimit(pid, RLIMIT_NOFILE, &rlim, &prev_limit), 0);
  // Verify that we can create and destroy more than rlimit_max
  for (int i = 0; i < 100; i++) {
    threshold = NumFilesUsed(pid);
    auto posix_ee = PosixEventEngine::MakePosixEventEngine();
    Connection connected_eps = CreateConnectedEndpoints(posix_ee);
    // Destroy both endpoints
    connected_eps.client_endpoint.reset();
    connected_eps.server_endpoint.reset();
    // Wait for event engine to shutdown.
    grpc_core::WaitForSingleOwner(std::move(posix_ee));
    // Wait until all newly created files are garbage collected by the kernel.
    WaitUntilNumFilesUsedDropsBelowThreshold(pid, threshold);
  }
}

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