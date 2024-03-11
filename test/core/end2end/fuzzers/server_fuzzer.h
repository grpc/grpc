// Copyright 2024 gRPC authors.
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

#ifndef GRPC_TEST_CORE_END2END_FUZZERS_SERVER_FUZZER_H
#define GRPC_TEST_CORE_END2END_FUZZERS_SERVER_FUZZER_H

#include "absl/functional/function_ref.h"

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/end2end/fuzzers/fuzzer_input.pb.h"

namespace grpc_core {

void RunServerFuzzer(
    const fuzzer_input::Msg& msg,
    absl::FunctionRef<void(grpc_server*, int, const ChannelArgs&)>
        server_setup);

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_END2END_FUZZERS_SERVER_FUZZER_H
