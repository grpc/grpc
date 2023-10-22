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

#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/inproc/legacy_inproc_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/metadata_batch.h"
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
};

class InprocClientTransport;
class InprocServerTransport;

class InprocClientTransport final : public InprocTransport,
                                    public ClientTransport {
 public:
  explicit InprocClientTransport(RefCountedPtr<InprocServerTransport> server);

  ClientTransport* client_transport() override { return this; }
  ServerTransport* server_transport() override { return nullptr; }

  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args) override;

  void Orphan() override { delete this; }

  void PerformOp(grpc_transport_op* op) final;

 private:
  InprocServerTransport* server() { return server_.get(); }

  RefCountedPtr<InprocServerTransport> server_;
};

class InprocServerTransport final
    : public InprocTransport,
      public ServerTransport,
      public RefCounted<InprocServerTransport, NonPolymorphicRefCount> {
 public:
  ClientTransport* client_transport() override { return nullptr; }
  ServerTransport* server_transport() override { return this; }

  ArenaPromise<ServerMetadataHandle> MakeCallPromise(CallArgs client_call_args);

  void Orphan() override { Unref(); }

  void PerformOp(grpc_transport_op* op) final;

 private:
  AcceptFn accept_fn_;
};

InprocClientTransport::InprocClientTransport(
    RefCountedPtr<InprocServerTransport> server)
    : server_(server) {}

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
                    return server_call->OnClientMessage(std::move(message));
                  }),
          [server_call] { return server_call->OnClientClose(); }),
      [](Empty) {});
  return Seq(
      TrySeq(
          server_call->GetServerInitialMetadata(),
          [server_initial_metadata_pipe =
               client_call_args.server_initial_metadata](
              ServerMetadataHandle server_initial_metadata) {
            return Map(server_initial_metadata_pipe->Push(
                           std::move(server_initial_metadata)),
                       [](bool ok) { return StatusFlag(ok); });
          },
          [server_call, server_to_client_message_pipe =
                            client_call_args.server_to_client_messages] {
            return ForEach(
                ServerMessageReader(server_call),
                [server_to_client_message_pipe](MessageHandle message) {
                  return Map(
                      server_to_client_message_pipe->Push(std::move(message)),
                      [](bool ok) {
                        return ok ? absl::OkStatus() : absl::CancelledError();
                      });
                });
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

RefCountedPtr<Channel> CreateInprocChannel(Server* server, ChannelArgs args) {
  auto server_transport = MakeRefCounted<InprocServerTransport>();
  auto client_transport =
      MakeOrphanable<InprocClientTransport>(server_transport);
  auto setup_error =
      server->SetupTransport(server_transport.get(), nullptr,
                             server->channel_args()
                                 .Remove(GRPC_ARG_MAX_CONNECTION_IDLE_MS)
                                 .Remove(GRPC_ARG_MAX_CONNECTION_AGE_MS),
                             nullptr);
  if (!setup_error.ok()) return nullptr;
  return Channel::Create("inproc", args, GRPC_CLIENT_DIRECT_CHANNEL,
                         client_transport.release())
      .value_or(nullptr);
}

}  // namespace

}  // namespace grpc_core

grpc_channel* grpc_inproc_channel_create(grpc_server* server,
                                         const grpc_channel_args* args,
                                         void* reserved) {
  if (!grpc_core::UsePromiseBasedTransport()) {
    return grpc_legacy_inproc_channel_create(server, args, reserved);
  }
  auto p = grpc_core::CreateInprocChannel(grpc_core::Server::FromC(server),
                                          grpc_core::CoreConfiguration::Get()
                                              .channel_args_preconditioning()
                                              .PreconditionChannelArgs(args));
  if (p != nullptr) return p.release()->c_ptr();
  return grpc_lame_client_channel_create(nullptr, GRPC_STATUS_UNAVAILABLE,
                                         "Failed to create client channel");
}
