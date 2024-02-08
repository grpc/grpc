//
//
// Copyright 2017 gRPC authors.
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
//
//

//******************************************************************************
// This test verifies that various stack configurations result in the set of
// filters that we expect.
//
// This is akin to a golden-file test, and suffers the same disadvantages and
// advantages: it reflects that the code as written has not been modified - and
// valid code modifications WILL break this test and it will need updating.
//
// The intent therefore is to allow code reviewers to more easily catch changes
// that perturb the generated list of channel filters in different
// configurations and assess whether such a change is correct and desirable.
//

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder_impl.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/util/test_config.h"

namespace {
class FakeTransport final : public grpc_core::Transport {
 public:
  explicit FakeTransport(absl::string_view transport_name)
      : transport_name_(transport_name) {}

  grpc_core::FilterStackTransport* filter_stack_transport() override {
    return nullptr;
  }
  grpc_core::ClientTransport* client_transport() override { return nullptr; }
  grpc_core::ServerTransport* server_transport() override { return nullptr; }

  absl::string_view GetTransportName() const override {
    return transport_name_;
  }
  void SetPollset(grpc_stream*, grpc_pollset*) override {}
  void SetPollsetSet(grpc_stream*, grpc_pollset_set*) override {}
  void PerformOp(grpc_transport_op*) override {}
  grpc_endpoint* GetEndpoint() override { return nullptr; }
  void Orphan() override {}

 private:
  absl::string_view transport_name_;
};
}  // namespace

std::vector<std::string> MakeStack(const char* transport_name,
                                   grpc_core::ChannelArgs channel_args,
                                   grpc_channel_stack_type channel_stack_type) {
  // create phony channel stack
  std::unique_ptr<FakeTransport> fake_transport;
  if (transport_name != nullptr) {
    fake_transport = absl::make_unique<FakeTransport>(transport_name);
    channel_args = channel_args.SetObject(fake_transport.get());
  }
  grpc_core::ChannelStackBuilderImpl builder("test", channel_stack_type,
                                             channel_args);
  builder.SetTarget("foo.test.google.fr");
  {
    grpc_core::ExecCtx exec_ctx;
    GPR_ASSERT(grpc_core::CoreConfiguration::Get().channel_init().CreateStack(
        &builder));
  }

  std::vector<std::string> parts;
  for (const auto& entry : *builder.mutable_stack()) {
    const char* name = entry->name;
    if (name == nullptr) continue;
    parts.push_back(name);
  }

  return parts;
}

TEST(ChannelStackFilters, LooksAsExpected) {
  const auto minimal_stack_args =
      grpc_core::ChannelArgs().Set(GRPC_ARG_MINIMAL_STACK, true);
  const auto no_args = grpc_core::ChannelArgs();

  EXPECT_EQ(
      MakeStack("unknown", minimal_stack_args, GRPC_CLIENT_DIRECT_CHANNEL),
      std::vector<std::string>({"authority", "connected"}));
  EXPECT_EQ(MakeStack("unknown", minimal_stack_args, GRPC_CLIENT_SUBCHANNEL),
            std::vector<std::string>({"authority", "connected"}));
  EXPECT_EQ(
      MakeStack("unknown", minimal_stack_args, GRPC_SERVER_CHANNEL),
      std::vector<std::string>({"server", "server_call_tracer", "connected"}));

  EXPECT_EQ(MakeStack("chttp2", minimal_stack_args, GRPC_CLIENT_DIRECT_CHANNEL),
            std::vector<std::string>(
                {"authority", "http-client", "compression", "connected"}));
  EXPECT_EQ(MakeStack("chttp2", minimal_stack_args, GRPC_CLIENT_SUBCHANNEL),
            std::vector<std::string>(
                {"authority", "http-client", "compression", "connected"}));
  EXPECT_EQ(MakeStack("chttp2", minimal_stack_args, GRPC_SERVER_CHANNEL),
            std::vector<std::string>({"server", "http-server", "compression",
                                      "server_call_tracer", "connected"}));
  EXPECT_EQ(MakeStack(nullptr, minimal_stack_args, GRPC_CLIENT_CHANNEL),
            std::vector<std::string>({"client-channel"}));

  // tests with a default stack

  EXPECT_EQ(MakeStack("unknown", no_args, GRPC_CLIENT_DIRECT_CHANNEL),
            std::vector<std::string>(
                {"authority", "message_size", "deadline", "connected"}));
  EXPECT_EQ(
      MakeStack("unknown", no_args, GRPC_CLIENT_SUBCHANNEL),
      std::vector<std::string>({"authority", "message_size", "connected"}));
  EXPECT_EQ(MakeStack("unknown", no_args, GRPC_SERVER_CHANNEL),
            std::vector<std::string>({"server", "message_size", "deadline",
                                      "server_call_tracer", "connected"}));

  EXPECT_EQ(
      MakeStack("chttp2", no_args, GRPC_CLIENT_DIRECT_CHANNEL),
      std::vector<std::string>({"authority", "message_size", "deadline",
                                "http-client", "compression", "connected"}));
  EXPECT_EQ(
      MakeStack("chttp2", no_args, GRPC_CLIENT_SUBCHANNEL),
      std::vector<std::string>({"authority", "message_size", "http-client",
                                "compression", "connected"}));

  EXPECT_EQ(MakeStack("chttp2", no_args, GRPC_SERVER_CHANNEL),
            std::vector<std::string>({"server", "message_size", "deadline",
                                      "http-server", "compression",
                                      "server_call_tracer", "connected"}));
  EXPECT_EQ(MakeStack(nullptr, no_args, GRPC_CLIENT_CHANNEL),
            grpc_core::IsClientIdlenessEnabled()
                ? std::vector<std::string>({"client_idle", "client-channel"})
                : std::vector<std::string>({"client-channel"}));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
