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

#ifndef GRPC_TEST_CORE_END2END_FUZZERS_CONNECTOR_FUZZER_H
#define GRPC_TEST_CORE_END2END_FUZZERS_CONNECTOR_FUZZER_H

#include "absl/functional/function_ref.h"
#include "src/core/client_channel/connector.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "test/core/end2end/fuzzers/fuzzer_input.pb.h"

namespace grpc_core {

void RunConnectorFuzzer(
    const fuzzer_input::Msg& msg,
    absl::FunctionRef<RefCountedPtr<grpc_channel_security_connector>()>
        make_security_connector,
    absl::FunctionRef<OrphanablePtr<SubchannelConnector>()> make_connector);

}

#endif  // GRPC_TEST_CORE_END2END_FUZZERS_CONNECTOR_FUZZER_H
