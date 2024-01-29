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

#include <gtest/gtest.h>

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/gprpp/env.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/transport/test_suite/fixture.h"
#include "test/core/transport/test_suite/fuzzer.pb.h"
#include "test/core/transport/test_suite/test.h"
#include "test/core/util/fuzz_config_vars.h"
#include "test/core/util/proto_bit_gen.h"

bool squelch = true;
static void dont_log(gpr_log_func_args* /*args*/) {}

DEFINE_PROTO_FUZZER(const transport_test_suite::Msg& msg) {
  const auto& tests = grpc_core::TransportTestRegistry::Get().tests();
  const auto& fixtures = grpc_core::TransportFixtureRegistry::Get().fixtures();
  GPR_ASSERT(!tests.empty());
  GPR_ASSERT(!fixtures.empty());
  const int test_id = msg.test_id() % tests.size();
  const int fixture_id = msg.fixture_id() % fixtures.size();

  if (squelch && !grpc_core::GetEnv("GRPC_TRACE_FUZZER").has_value()) {
    gpr_set_log_function(dont_log);
  }

  grpc_core::ConfigVars::Overrides overrides =
      grpc_core::OverridesFromFuzzConfigVars(msg.config_vars());
  grpc_core::ConfigVars::SetOverrides(overrides);
  grpc_core::TestOnlyReloadExperimentsFromConfigVariables();
  if (!squelch) {
    fprintf(stderr, "RUN TEST '%s' with fixture '%s'\n",
            std::string(tests[test_id].name).c_str(),
            std::string(fixtures[fixture_id].name).c_str());
  }
  grpc_core::ProtoBitGen bitgen(msg.rng());
  auto test =
      tests[test_id].create(std::unique_ptr<grpc_core::TransportFixture>(
                                fixtures[fixture_id].create()),
                            msg.event_engine_actions(), bitgen);
  test->RunTest();
  delete test;
  GPR_ASSERT(!::testing::Test::HasFailure());
}
