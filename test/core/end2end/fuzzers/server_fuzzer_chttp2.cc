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

#include <grpc/grpc_security.h>

#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/end2end/fuzzers/server_fuzzer.h"

DEFINE_PROTO_FUZZER(const fuzzer_input::Msg& msg) {
  grpc_core::RunServerFuzzer(msg, [](grpc_server* server, int port_num,
                                     const grpc_core::ChannelArgs&) {
    auto* creds = grpc_insecure_server_credentials_create();
    grpc_server_add_http2_port(
        server, absl::StrCat("0.0.0.0:", port_num).c_str(), creds);
    grpc_server_credentials_release(creds);
  });
}
