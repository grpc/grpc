// Copyright 2023 gRPC authors.
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

#include <stdio.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/end2end/end2end_test_fuzzer.pb.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/h2_tls_common.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/util/fuzz_config_vars.h"

using ::grpc_event_engine::experimental::FuzzingEventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;

namespace grpc_event_engine {
namespace experimental {
extern bool g_event_engine_supports_fd;
}
}  // namespace grpc_event_engine

bool squelch = true;
static void dont_log(gpr_log_func_args* /*args*/) {}

int force_experiments = []() {
  grpc_event_engine::experimental::g_event_engine_supports_fd = false;
  grpc_core::ForceEnableExperiment("event_engine_client", true);
  grpc_core::ForceEnableExperiment("event_engine_listener", true);
  return 1;
}();

DEFINE_PROTO_FUZZER(const core_end2end_test_fuzzer::Msg& msg) {
  static const auto all_tests =
      grpc_core::CoreEnd2endTestRegistry::Get().AllTests();
  static const auto tests = []() {
    std::map<absl::string_view,
             std::map<absl::string_view,
                      std::map<absl::string_view,
                               absl::AnyInvocable<std::unique_ptr<
                                   grpc_core::CoreEnd2endTest>() const>>>>
        tests;
    for (const auto& test : all_tests) {
      tests[test.suite][test.name].emplace(test.config->name, [&test]() {
        return std::unique_ptr<grpc_core::CoreEnd2endTest>(
            test.make_test(test.config));
      });
    }
    return tests;
  }();

  if (squelch && !grpc_core::GetEnv("GRPC_TRACE_FUZZER").has_value()) {
    gpr_set_log_function(dont_log);
  }

  auto suite_it = tests.find(msg.suite());
  if (suite_it == tests.end()) return;
  auto test_it = suite_it->second.find(msg.test());
  if (test_it == suite_it->second.end()) return;
  auto config_it = test_it->second.find(msg.config());
  if (config_it == test_it->second.end()) return;

  // TODO(ctiller): make this per fixture?
  grpc_core::ConfigVars::Overrides overrides =
      grpc_core::OverridesFromFuzzConfigVars(msg.config_vars());
  overrides.default_ssl_roots_file_path = CA_CERT_PATH;
  grpc_core::ConfigVars::SetOverrides(overrides);
  grpc_event_engine::experimental::SetEventEngineFactory(
      [actions = msg.event_engine_actions()]() {
        return std::make_unique<FuzzingEventEngine>(
            FuzzingEventEngine::Options(), actions);
      });
  auto engine =
      std::dynamic_pointer_cast<FuzzingEventEngine>(GetDefaultEventEngine());

  fprintf(stderr, "%s/%s/%s\n", msg.suite().c_str(), msg.test().c_str(),
          msg.config().c_str());

  auto test = config_it->second();
  test->SetPostGrpcInitFunc([]() {
    grpc_timer_manager_set_threading(false);
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Executor::SetThreadingAll(false);
  });
  test->SetUp();
  test->RunTest();
  test->TearDown();

  engine->UnsetGlobalHooks();
}
