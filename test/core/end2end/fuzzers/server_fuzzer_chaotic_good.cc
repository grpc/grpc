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

#include "src/core/ext/transport/chaotic_good/server/chaotic_good_server.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/end2end/fuzzers/server_fuzzer.h"

DEFINE_PROTO_FUZZER(const fuzzer_input::Msg& msg) {
  grpc_core::RunServerFuzzer(
      msg, [](grpc_server* server, int port_num,
              const grpc_core::ChannelArgs& channel_args) {
        grpc_core::ExecCtx exec_ctx;
        auto* listener = new grpc_core::chaotic_good::ChaoticGoodServerListener(
            grpc_core::Server::FromC(server), channel_args,
            [next = uint64_t(0)]() mutable {
              return absl::StrCat(absl::Hex(next++));
            });
        auto port = listener->Bind(
            grpc_event_engine::experimental::URIToResolvedAddress(
                absl::StrCat("ipv4:0.0.0.0:", port_num))
                .value());
        GPR_ASSERT(port.ok());
        GPR_ASSERT(port.value() == port_num);
        grpc_core::Server::FromC(server)->AddListener(
            grpc_core::OrphanablePtr<
                grpc_core::chaotic_good::ChaoticGoodServerListener>(listener));
      });
}
