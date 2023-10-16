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

#ifndef GRPC_TEST_CORE_END2END_FUZZERS_NETWORK_INPUT_H
#define GRPC_TEST_CORE_END2END_FUZZERS_NETWORK_INPUT_H

#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "test/core/end2end/fuzzers/fuzzer_input.pb.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"

namespace grpc_core {

Duration ScheduleReads(
    const fuzzer_input::NetworkInput& network_input,
    grpc_endpoint* mock_endpoint,
    grpc_event_engine::experimental::FuzzingEventEngine* event_engine);

}

#endif  // GRPC_TEST_CORE_END2END_FUZZERS_NETWORK_INPUT_H
