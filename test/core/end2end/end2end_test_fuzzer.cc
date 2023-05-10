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

#include <stddef.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "absl/functional/any_invocable.h"
#include "absl/strings/str_cat.h"
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
#include "test/core/util/fuzz_config_vars.pb.h"
#include "test/core/util/osa_distance.h"

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
  grpc_core::g_is_fuzzing_core_e2e_tests = true;

  struct Test {
    std::string name;
    absl::AnyInvocable<std::unique_ptr<grpc_core::CoreEnd2endTest>() const>
        factory;
  };

  static const auto all_tests =
      grpc_core::CoreEnd2endTestRegistry::Get().AllTests();
  static const auto tests = []() {
    auto only_suite = grpc_core::GetEnv("GRPC_TEST_FUZZER_SUITE");
    auto only_test = grpc_core::GetEnv("GRPC_TEST_FUZZER_TEST");
    auto only_config = grpc_core::GetEnv("GRPC_TEST_FUZZER_CONFIG");
    std::vector<Test> tests;
    for (const auto& test : all_tests) {
      if (test.config->feature_mask & FEATURE_MASK_DO_NOT_FUZZ) continue;
      if (only_suite.has_value() && test.suite != only_suite.value()) continue;
      if (only_test.has_value() && test.name != only_test.value()) continue;
      if (only_config.has_value() && test.config->name != only_config.value()) {
        continue;
      }
      tests.emplace_back(
          Test{absl::StrCat(test.suite, ".", test.name, "/", test.config->name),
               [&test]() {
                 return std::unique_ptr<grpc_core::CoreEnd2endTest>(
                     test.make_test(test.config));
               }});
    }
    GPR_ASSERT(!tests.empty());
    return tests;
  }();
  static const auto only_experiment =
      grpc_core::GetEnv("GRPC_TEST_FUZZER_EXPERIMENT");

  if (squelch && !grpc_core::GetEnv("GRPC_TRACE_FUZZER").has_value()) {
    gpr_set_log_function(dont_log);
  }

  auto test_name =
      absl::StrCat(msg.suite(), ".", msg.test(), "/", msg.config());
  size_t best_test = 0;
  for (size_t i = 0; i < tests.size(); i++) {
    if (grpc_core::OsaDistance(test_name, tests[i].name) <
        grpc_core::OsaDistance(test_name, tests[best_test].name)) {
      best_test = i;
    }
  }

  if (only_experiment.has_value() &&
      msg.config_vars().experiments() != only_experiment.value()) {
    return;
  }

  // TODO(ctiller): make this per fixture?
  grpc_core::ConfigVars::Overrides overrides =
      grpc_core::OverridesFromFuzzConfigVars(msg.config_vars());
  overrides.default_ssl_roots_file_path = CA_CERT_PATH;
  grpc_core::ConfigVars::SetOverrides(overrides);
  grpc_event_engine::experimental::SetEventEngineFactory(
      [actions = msg.event_engine_actions()]() {
        FuzzingEventEngine::Options options;
        options.max_delay_run_after = std::chrono::milliseconds(1500);
        return std::make_unique<FuzzingEventEngine>(options, actions);
      });
  auto engine =
      std::dynamic_pointer_cast<FuzzingEventEngine>(GetDefaultEventEngine());

  auto test = tests[best_test].factory();
  test->SetCrashOnStepFailure();
  test->SetQuiesceEventEngine(
      [](std::shared_ptr<grpc_event_engine::experimental::EventEngine>&& ee) {
        static_cast<FuzzingEventEngine*>(ee.get())->TickUntilIdle();
      });
  test->SetCqVerifierStepFn(
      [engine = std::move(engine)](
          grpc_event_engine::experimental::EventEngine::Duration max_step) {
        grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
        grpc_core::ExecCtx exec_ctx;
        engine->Tick(max_step);
        grpc_timer_manager_tick();
      });
  test->SetPostGrpcInitFunc([]() {
    grpc_timer_manager_set_threading(false);
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Executor::SetThreadingAll(false);
  });
  test->SetUp();
  test->RunTest();
  test->TearDown();
  GPR_ASSERT(!::testing::Test::HasFailure());
}
