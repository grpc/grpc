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

#include "src/core/ext/transport/inproc/inproc_transport.h"

#include <atomic>

#include "absl/log/check.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/inproc/legacy_inproc_transport.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/surface/channel_create.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/server/server.h"

namespace grpc_core {

namespace {
class InprocClientTransport;

class InprocServerTransport final : public ServerTransport {
 public:
  explicit InprocServerTransport(const ChannelArgs& args)
      : event_engine_(
            args.GetObjectRef<grpc_event_engine::experimental::EventEngine>()),
        call_arena_allocator_(MakeRefCounted<CallArenaAllocator>(
            args.GetObject<ResourceQuota>()
                ->memory_quota()
                ->CreateMemoryAllocator("inproc_server"),
            1024)) {}

  void SetCallDestination(
      RefCountedPtr<UnstartedCallDestination> unstarted_call_handler) override {
    unstarted_call_handler_ = unstarted_call_handler;
    ConnectionState expect = ConnectionState::kInitial;
    state_.compare_exchange_strong(expect, ConnectionState::kReady,
                                   std::memory_order_acq_rel,
                                   std::memory_order_acquire);
    MutexLock lock(&state_tracker_mu_);
    state_tracker_.SetState(GRPC_CHANNEL_READY, absl::OkStatus(),
                            "accept function set");
  }

  void Orphan() override { Unref(); }

  FilterStackTransport* filter_stack_transport() override { return nullptr; }
  ClientTransport* client_transport() override { return nullptr; }
  ServerTransport* server_transport() override { return this; }
  absl::string_view GetTransportName() const override { return "inproc"; }
  void SetPollset(grpc_stream*, grpc_pollset*) override {}
  void SetPollsetSet(grpc_stream*, grpc_pollset_set*) override {}
  void PerformOp(grpc_transport_op* op) override {
    GRPC_TRACE_LOG(inproc, INFO)
        << "inproc server op: " << grpc_transport_op_string(op);
    if (op->start_connectivity_watch != nullptr) {
      MutexLock lock(&state_tracker_mu_);
      state_tracker_.AddWatcher(op->start_connectivity_watch_state,
                                std::move(op->start_connectivity_watch));
    }
    if (op->stop_connectivity_watch != nullptr) {
      MutexLock lock(&state_tracker_mu_);
      state_tracker_.RemoveWatcher(op->stop_connectivity_watch);
    }
    if (op->set_accept_stream) {
      Crash("set_accept_stream not supported on inproc transport");
    }
  }

  void Disconnect(absl::Status error) {
    if (disconnecting_.exchange(true, std::memory_order_relaxed)) return;
    disconnect_error_ = std::move(error);
    state_.store(ConnectionState::kDisconnected, std::memory_order_relaxed);
    MutexLock lock(&state_tracker_mu_);
    state_tracker_.SetState(GRPC_CHANNEL_SHUTDOWN, disconnect_error_,
                            "inproc transport disconnected");
  }

  absl::StatusOr<CallInitiator> AcceptCall(ClientMetadataHandle md) {
    switch (state_.load(std::memory_order_acquire)) {
      case ConnectionState::kInitial:
        return absl::InternalError(
            "inproc transport hasn't started accepting calls");
      case ConnectionState::kDisconnected:
        return absl::UnavailableError("inproc transport is disconnected");
      case ConnectionState::kReady:
        break;
    }
    auto server_call = MakeCallPair(std::move(md), event_engine_.get(),
                                    call_arena_allocator_->MakeArena());
    unstarted_call_handler_->StartCall(std::move(server_call.handler));
    return std::move(server_call.initiator);
  }

  OrphanablePtr<InprocClientTransport> MakeClientTransport();

 private:
  enum class ConnectionState : uint8_t { kInitial, kReady, kDisconnected };

  std::atomic<ConnectionState> state_{ConnectionState::kInitial};
  std::atomic<bool> disconnecting_{false};
  RefCountedPtr<UnstartedCallDestination> unstarted_call_handler_;
  absl::Status disconnect_error_;
  Mutex state_tracker_mu_;
  ConnectivityStateTracker state_tracker_ ABSL_GUARDED_BY(state_tracker_mu_){
      "inproc_server_transport", GRPC_CHANNEL_CONNECTING};
  const std::shared_ptr<grpc_event_engine::experimental::EventEngine>
      event_engine_;
  const RefCountedPtr<CallArenaAllocator> call_arena_allocator_;
};

class InprocClientTransport final : public ClientTransport {
 public:
  explicit InprocClientTransport(
      RefCountedPtr<InprocServerTransport> server_transport)
      : server_transport_(std::move(server_transport)) {}

  void StartCall(CallHandler child_call_handler) override {
    child_call_handler.SpawnGuarded(
        "pull_initial_metadata",
        TrySeq(child_call_handler.PullClientInitialMetadata(),
               [server_transport = server_transport_,
                child_call_handler](ClientMetadataHandle md) {
                 auto server_call_initiator =
                     server_transport->AcceptCall(std::move(md));
                 if (!server_call_initiator.ok()) {
                   return server_call_initiator.status();
                 }
                 ForwardCall(child_call_handler,
                             std::move(*server_call_initiator));
                 return absl::OkStatus();
               }));
  }

  void Orphan() override { delete this; }

  FilterStackTransport* filter_stack_transport() override { return nullptr; }
  ClientTransport* client_transport() override { return this; }
  ServerTransport* server_transport() override { return nullptr; }
  absl::string_view GetTransportName() const override { return "inproc"; }
  void SetPollset(grpc_stream*, grpc_pollset*) override {}
  void SetPollsetSet(grpc_stream*, grpc_pollset_set*) override {}
  void PerformOp(grpc_transport_op*) override { Crash("unimplemented"); }

 private:
  ~InprocClientTransport() override {
    server_transport_->Disconnect(
        absl::UnavailableError("Client transport closed"));
  }

  const RefCountedPtr<InprocServerTransport> server_transport_;
};

bool UsePromiseBasedTransport() {
  return IsPromiseBasedInprocTransportEnabled();
}

OrphanablePtr<InprocClientTransport>
InprocServerTransport::MakeClientTransport() {
  return MakeOrphanable<InprocClientTransport>(
      RefAsSubclass<InprocServerTransport>());
}

RefCountedPtr<Channel> MakeLameChannel(absl::string_view why,
                                       absl::Status error) {
  gpr_log(GPR_ERROR, "%s: %s", std::string(why).c_str(),
          std::string(error.message()).c_str());
  intptr_t integer;
  grpc_status_code status = GRPC_STATUS_INTERNAL;
  if (grpc_error_get_int(error, StatusIntProperty::kRpcStatus, &integer)) {
    status = static_cast<grpc_status_code>(integer);
  }
  return RefCountedPtr<Channel>(Channel::FromC(grpc_lame_client_channel_create(
      nullptr, status, std::string(why).c_str())));
}

RefCountedPtr<Channel> MakeInprocChannel(Server* server,
                                         ChannelArgs client_channel_args) {
  auto transports = MakeInProcessTransportPair(server->channel_args());
  auto client_transport = std::move(transports.first);
  auto server_transport = std::move(transports.second);
  auto error =
      server->SetupTransport(server_transport.get(), nullptr,
                             server->channel_args()
                                 .Remove(GRPC_ARG_MAX_CONNECTION_IDLE_MS)
                                 .Remove(GRPC_ARG_MAX_CONNECTION_AGE_MS),
                             nullptr);
  if (!error.ok()) {
    return MakeLameChannel("Failed to create server channel", std::move(error));
  }
  std::ignore = server_transport.release();  // consumed by SetupTransport
  auto channel = ChannelCreate(
      "inproc",
      client_channel_args.Set(GRPC_ARG_DEFAULT_AUTHORITY, "inproc.authority"),
      GRPC_CLIENT_DIRECT_CHANNEL, client_transport.release());
  if (!channel.ok()) {
    return MakeLameChannel("Failed to create client channel", channel.status());
  }
  return std::move(*channel);
}
}  // namespace

std::pair<OrphanablePtr<Transport>, OrphanablePtr<Transport>>
MakeInProcessTransportPair(const ChannelArgs& server_channel_args) {
  auto server_transport =
      MakeOrphanable<InprocServerTransport>(server_channel_args);
  auto client_transport = server_transport->MakeClientTransport();
  return std::make_pair(std::move(client_transport),
                        std::move(server_transport));
}

}  // namespace grpc_core

grpc_channel* grpc_inproc_channel_create(grpc_server* server,
                                         const grpc_channel_args* args,
                                         void* reserved) {
  grpc_core::ApplicationCallbackExecCtx app_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  if (!grpc_core::UsePromiseBasedTransport()) {
    return grpc_legacy_inproc_channel_create(server, args, reserved);
  }
  return grpc_core::MakeInprocChannel(grpc_core::Server::FromC(server),
                                      grpc_core::CoreConfiguration::Get()
                                          .channel_args_preconditioning()
                                          .PreconditionChannelArgs(args))
      .release()
      ->c_ptr();
}
