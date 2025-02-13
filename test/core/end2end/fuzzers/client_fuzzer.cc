// Copyright 2016 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>

#include <optional>
#include <string>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "fuzztest/fuzztest.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_create.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/env.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/end2end/fuzzers/api_fuzzer.pb.h"
#include "test/core/end2end/fuzzers/fuzzer_input.pb.h"
#include "test/core/end2end/fuzzers/fuzzing_common.h"
#include "test/core/end2end/fuzzers/network_input.h"
#include "test/core/test_util/fuzz_config_vars.h"
#include "test/core/test_util/fuzz_config_vars_helpers.h"
#include "test/core/test_util/mock_endpoint.h"
#include "test/core/test_util/test_config.h"

bool squelch = true;
bool leak_check = true;

static void discard_write(grpc_slice /*slice*/) {}

namespace grpc_core {
namespace testing {

class ClientFuzzer final : public BasicFuzzer {
 public:
  explicit ClientFuzzer(const fuzzer_input::Msg& msg)
      : BasicFuzzer(msg.event_engine_actions()),
        mock_endpoint_controller_(
            grpc_event_engine::experimental::MockEndpointController::Create(
                engine())) {
    ExecCtx exec_ctx;
    UpdateMinimumRunTime(ScheduleReads(
        msg.network_input()[0], mock_endpoint_controller_, engine().get()));
    ChannelArgs args =
        CoreConfiguration::Get()
            .channel_args_preconditioning()
            .PreconditionChannelArgs(nullptr)
            .SetIfUnset(GRPC_ARG_DEFAULT_AUTHORITY, "test-authority");
    Transport* transport = grpc_create_chttp2_transport(
        args,
        OrphanablePtr<grpc_endpoint>(
            mock_endpoint_controller_->TakeCEndpoint()),
        true);
    channel_ = ChannelCreate("test-target", args, GRPC_CLIENT_DIRECT_CHANNEL,
                             transport)
                   ->release()
                   ->c_ptr();
  }

  ~ClientFuzzer() { CHECK_EQ(channel_, nullptr); }

 private:
  Result CreateChannel(const api_fuzzer::CreateChannel&) override {
    return Result::kFailed;
  }
  Result CreateServer(const api_fuzzer::CreateServer&) override {
    return Result::kFailed;
  }
  void DestroyServer() override {}
  void DestroyChannel() override {
    grpc_channel_destroy(channel_);
    channel_ = nullptr;
  }

  grpc_server* server() override { return nullptr; }
  grpc_channel* channel() override { return channel_; }

  std::shared_ptr<grpc_event_engine::experimental::MockEndpointController>
      mock_endpoint_controller_;
  grpc_channel* channel_ = nullptr;
};

void Run(fuzzer_input::Msg msg) {
  if (squelch && !GetEnv("GRPC_TRACE_FUZZER").has_value()) {
    grpc_disable_all_absl_logs();
  }
  if (msg.network_input().size() != 1) return;
  ApplyFuzzConfigVars(msg.config_vars());
  TestOnlyReloadExperimentsFromConfigVariables();
  testing::ClientFuzzer(msg).Run(msg.api_actions());
}
FUZZ_TEST(ClientFuzzerTest, Run)
    .WithDomains(::fuzztest::Arbitrary<fuzzer_input::Msg>().WithProtobufField(
        "config_vars", AnyConfigVars()));

}  // namespace testing
}  // namespace grpc_core
