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

#include <string>

#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/transport.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/end2end/fuzzers/api_fuzzer.pb.h"
#include "test/core/end2end/fuzzers/fuzzer_input.pb.h"
#include "test/core/end2end/fuzzers/fuzzing_common.h"
#include "test/core/end2end/fuzzers/network_input.h"
#include "test/core/util/fuzz_config_vars.h"
#include "test/core/util/fuzzing_channel_args.h"
#include "test/core/util/mock_endpoint.h"

bool squelch = true;
bool leak_check = true;

static void discard_write(grpc_slice /*slice*/) {}

static void dont_log(gpr_log_func_args* /*args*/) {}

namespace grpc_core {
namespace testing {

class ServerFuzzer final : public BasicFuzzer {
 public:
  explicit ServerFuzzer(const fuzzer_input::Msg& msg)
      : BasicFuzzer(msg.event_engine_actions()) {
    ExecCtx exec_ctx;
    UpdateMinimumRunTime(
        ScheduleReads(msg.network_input(), mock_endpoint_, engine()));
    grpc_server_register_completion_queue(server_, cq(), nullptr);
    // TODO(ctiller): add more registered methods (one for POST, one for PUT)
    grpc_server_register_method(server_, "/reg", nullptr, {}, 0);
    grpc_server_start(server_);
    ChannelArgs channel_args =
        CoreConfiguration::Get()
            .channel_args_preconditioning()
            .PreconditionChannelArgs(
                CreateChannelArgsFromFuzzingConfiguration(
                    msg.channel_args(), FuzzingEnvironment{resource_quota()})
                    .ToC()
                    .get());
    Transport* transport =
        grpc_create_chttp2_transport(channel_args, mock_endpoint_, false);
    transport_setup_ok_ =
        Server::FromC(server_)
            ->SetupTransport(transport, nullptr, channel_args, nullptr)
            .ok();
    if (transport_setup_ok_) {
      grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
    } else {
      DestroyServer();
    }
  }

  ~ServerFuzzer() { GPR_ASSERT(server_ == nullptr); }

  bool transport_setup_ok() const { return transport_setup_ok_; }

 private:
  Result CreateChannel(
      const api_fuzzer::CreateChannel& /* create_channel */) override {
    return Result::kFailed;
  }
  Result CreateServer(
      const api_fuzzer::CreateServer& /* create_server */) override {
    return Result::kFailed;
  }
  void DestroyServer() override {
    grpc_server_destroy(server_);
    server_ = nullptr;
  }
  void DestroyChannel() override {}

  grpc_server* server() override { return server_; }
  grpc_channel* channel() override { return nullptr; }

  grpc_endpoint* mock_endpoint_ = grpc_mock_endpoint_create(discard_write);
  grpc_server* server_ = grpc_server_create(nullptr, nullptr);
  bool transport_setup_ok_ = false;
};

}  // namespace testing
}  // namespace grpc_core

DEFINE_PROTO_FUZZER(const fuzzer_input::Msg& msg) {
  if (squelch && !grpc_core::GetEnv("GRPC_TRACE_FUZZER").has_value()) {
    gpr_set_log_function(dont_log);
  }
  grpc_core::ApplyFuzzConfigVars(msg.config_vars());
  grpc_core::TestOnlyReloadExperimentsFromConfigVariables();
  grpc_core::testing::ServerFuzzer server_fuzzer(msg);
  if (!server_fuzzer.transport_setup_ok()) return;
  server_fuzzer.Run(msg.api_actions());
}
