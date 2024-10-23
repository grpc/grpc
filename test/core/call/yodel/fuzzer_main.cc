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

#include <grpc/event_engine/event_engine.h>
#include <gtest/gtest.h>
#include <stdio.h>

#include "absl/log/check.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/util/env.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/call/yodel/fuzzer.pb.h"
#include "test/core/call/yodel/yodel_test.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/test_util/fuzz_config_vars.h"
#include "test/core/test_util/proto_bit_gen.h"
#include "test/core/test_util/test_config.h"

bool squelch = true;

DEFINE_PROTO_FUZZER(const transport_test_suite::Msg& msg) {
  grpc_core::g_yodel_fuzzing = true;
  static const grpc_core::NoDestruct<
      std::vector<grpc_core::yodel_detail::TestRegistry::Test>>
      tests{grpc_core::yodel_detail::TestRegistry::AllTests()};
  CHECK(!tests->empty());
  const int test_id = msg.test_id() % tests->size();

  if (squelch && !grpc_core::GetEnv("GRPC_TRACE_FUZZER").has_value()) {
    grpc_disable_all_absl_logs();
  }

  grpc_core::ConfigVars::Overrides overrides =
      grpc_core::OverridesFromFuzzConfigVars(msg.config_vars());
  grpc_core::ConfigVars::SetOverrides(overrides);
  grpc_core::TestOnlyReloadExperimentsFromConfigVariables();
  if (!squelch) {
    LOG(INFO) << "RUN TEST '" << (*tests)[test_id].name << "'";
  }
  grpc_core::ProtoBitGen bitgen(msg.rng());
  auto test = (*tests)[test_id].make(msg.event_engine_actions(), bitgen);
  test->RunTest();
  delete test;
  CHECK(!::testing::Test::HasFailure());
}
