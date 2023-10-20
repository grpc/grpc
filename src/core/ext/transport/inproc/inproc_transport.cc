// Copyright 2017 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/inproc/inproc_transport.h"

#include <cstddef>
#include <memory>

#include <grpc/support/log.h>

#include "src/core/ext/transport/inproc/legacy_inproc_transport.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

namespace {

class InprocTransport : public Transport {
 public:
  FilterStackTransport* filter_stack_transport() final { return nullptr; }
  absl::string_view GetTransportName() const final { return "inproc"; }
  void SetPollset(grpc_stream* stream, grpc_pollset* pollset) final {}
  void SetPollsetSet(grpc_stream* stream, grpc_pollset_set* pollset_set) final {
  }
  grpc_endpoint* GetEndpoint() final { return nullptr; }
  void PerformOp(grpc_transport_op* op) final;
};

class InprocClientTransport;
class InprocServerTransport;

class InprocClientTransport final : public InprocTransport,
                                    public ClientTransport {
 public:
  ClientTransport* client_transport() override { return this; }
  ServerTransport* server_transport() override { return nullptr; }

  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args) override;

  void Orphan() override;

 private:
  InprocServerTransport* server();
};

class InprocServerTransport final : public InprocTransport,
                                    public ServerTransport {
 public:
  ClientTransport* client_transport() override { return nullptr; }
  ServerTransport* server_transport() override { return this; }

  ArenaPromise<ServerMetadataHandle> MakeCallPromise(CallArgs client_call_args);

  void Orphan() override;

 private:
  AcceptFn accept_fn_;
};

ArenaPromise<ServerMetadataHandle> InprocClientTransport::MakeCallPromise(
    CallArgs call_args) {
  return server()->MakeCallPromise(std::move(call_args));
}

ArenaPromise<ServerMetadataHandle> InprocServerTransport::MakeCallPromise(
    CallArgs client_call_args) {
  RefCountedPtr<CallPart> server_call =
      accept_fn_(std::move(client_call_args.client_initial_metadata));
  Party* client_party = static_cast<Party*>(Activity::current());
  client_party->Spawn(
      "client_to_server",
      Seq(ForEach(std::move(*client_call_args.client_to_server_messages),
                  [server_call](MessageHandle message) {
                    return Map(server_call->OnClientMessage(std::move(message)),
                               [](bool ok) { return StatusFlag(ok); });
                  }),
          [server_call] { return server_call->OnClientClose(); }),
      [](Empty) {});
  return Seq(
      server_call->GetServerInitialMetadata(),
      [server_initial_metadata_pipe = client_call_args.server_initial_metadata](
          ServerMetadataHandle server_initial_metadata) {
        return server_initial_metadata_pipe->Push(
            std::move(server_initial_metadata));
      },
      ForEach(ServerMessageReader(server_call),
              [server_to_client_message_pipe =
                   client_call_args.server_to_client_messages](
                  MessageHandle message) {
                return Map(
                    server_to_client_message_pipe->Push(std::move(message)),
                    [](bool ok) { return StatusFlag(ok); });
              }),
      [server_call] { return server_call->GetServerTrailingMetadata(); });
}

bool UsePromiseBasedTransport() {
  if (!IsPromiseBasedInprocTransportEnabled()) return false;
  if (!IsPromiseBasedClientCallEnabled()) {
    gpr_log(GPR_ERROR,
            "Promise based inproc transport requested but promise based client "
            "calls are disabled: using legacy implementation.");
    return false;
  }
  if (!IsPromiseBasedServerCallEnabled()) {
    gpr_log(GPR_ERROR,
            "Promise based inproc transport requested but promise based server "
            "calls are disabled: using legacy implementation.");
    return false;
  }
  return true;
}
}  // namespace

}  // namespace grpc_core

grpc_channel* grpc_inproc_channel_create(grpc_server* server,
                                         const grpc_channel_args* args,
                                         void* reserved) {
  if (!grpc_core::UsePromiseBasedTransport()) {
    return grpc_legacy_inproc_channel_create(server, args, reserved);
  }
  grpc_core::Crash("unimplemented");
}
