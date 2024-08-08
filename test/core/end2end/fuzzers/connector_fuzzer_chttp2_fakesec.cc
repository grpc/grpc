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

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#include "src/core/ext/transport/chttp2/client/chttp2_connector.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/security_connector/fake/fake_security_connector.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/end2end/fuzzers/connector_fuzzer.h"

DEFINE_PROTO_FUZZER(const fuzzer_input::Msg& msg) {
  grpc_core::RunConnectorFuzzer(
      msg,
      []() {
        return grpc_fake_channel_security_connector_create(
            grpc_core::RefCountedPtr<grpc_channel_credentials>(
                grpc_fake_transport_security_credentials_create()),
            nullptr, "foobar", grpc_core::ChannelArgs{});
      },
      []() { return grpc_core::MakeOrphanable<grpc_core::Chttp2Connector>(); });
}
