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
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/lib/experiments/config.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/end2end/fuzzers/api_fuzzer.pb.h"
#include "test/core/end2end/fuzzers/fuzzer_input.pb.h"
#include "test/core/end2end/fuzzers/fuzzing_common.h"
#include "test/core/end2end/fuzzers/network_input.h"
#include "test/core/util/fuzz_config_vars.h"

bool squelch = true;
bool leak_check = true;

static void dont_log(gpr_log_func_args* /*args*/) {}

namespace grpc_core {
namespace testing {

class ServerFuzzer final : public BasicFuzzer {
 public:
  explicit ServerFuzzer(const fuzzer_input::Msg& msg)
      : BasicFuzzer(msg.event_engine_actions()) {
    ExecCtx exec_ctx;
    grpc_server_register_completion_queue(server_, cq(), nullptr);
    // TODO(ctiller): add more registered methods (one for POST, one for PUT)
    grpc_server_register_method(server_, "/reg", nullptr, {}, 0);
    auto* creds = grpc_insecure_server_credentials_create();
    grpc_server_add_http2_port(server_, "0.0.0.0:1234", creds);
    grpc_server_credentials_release(creds);
    grpc_server_start(server_);
    for (const auto& input : msg.network_input()) {
      UpdateMinimumRunTime(ScheduleConnection(
          input, engine(), FuzzingEnvironment{resource_quota()}, 1234));
    }
  }

  ~ServerFuzzer() { GPR_ASSERT(server_ == nullptr); }

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

  grpc_server* server_ = grpc_server_create(nullptr, nullptr);
};

}  // namespace testing
}  // namespace grpc_core

DEFINE_PROTO_FUZZER(const fuzzer_input::Msg& msg) {
  if (squelch && !grpc_core::GetEnv("GRPC_TRACE_FUZZER").has_value()) {
    gpr_set_log_function(dont_log);
  }
  static const int once = []() {
    grpc_core::ForceEnableExperiment("event_engine_client", true);
    grpc_core::ForceEnableExperiment("event_engine_listener", true);
    return 42;
  }();
  GPR_ASSERT(once == 42);  // avoid unused variable warning
  grpc_core::ApplyFuzzConfigVars(msg.config_vars());
  grpc_core::TestOnlyReloadExperimentsFromConfigVariables();
  grpc_core::testing::ServerFuzzer(msg).Run(msg.api_actions());
}
