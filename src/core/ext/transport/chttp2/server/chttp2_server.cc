//
//
// Copyright 2015 gRPC authors.
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
//
//

#include "src/core/ext/transport/chttp2/server/chttp2_server.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/passive_listener.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "src/core/channelz/channelz.h"
#include "src/core/config/core_configuration.h"
#include "src/core/credentials/transport/insecure/insecure_credentials.h"
#include "src/core/credentials/transport/security_connector.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/legacy_frame.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/handshaker/handshaker_registry.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/extensions/supports_fd.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/event_engine/resolved_address_internal.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/iomgr/vsock.h"
#include "src/core/lib/resource_quota/connection_quota.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/server/server.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/match.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/util/uri.h"

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/tcp_client_posix.h"
#endif  // GPR_SUPPORT_CHANNELS_FROM_FD

namespace grpc_core {

using grpc_event_engine::experimental::ChannelArgsEndpointConfig;
using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::EventEngineSupportsFdExtension;
using grpc_event_engine::experimental::QueryExtension;
using http2::Http2ErrorCode;

const char kUnixUriPrefix[] = "unix:";
const char kUnixAbstractUriPrefix[] = "unix-abstract:";
const char kVSockUriPrefix[] = "vsock:";

namespace {
Timestamp GetConnectionDeadline(const ChannelArgs& args) {
  return Timestamp::Now() +
         std::max(
             Duration::Milliseconds(1),
             args.GetDurationFromIntMillis(GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS)
                 .value_or(Duration::Minutes(2)));
}
}  // namespace

using AcceptorPtr = std::unique_ptr<grpc_tcp_server_acceptor, AcceptorDeleter>;

//
// NewChttp2ServerListener::ActiveConnection::HandshakingState
//

NewChttp2ServerListener::ActiveConnection::HandshakingState::HandshakingState(
    RefCountedPtr<ActiveConnection> connection_ref, grpc_tcp_server* tcp_server,
    grpc_pollset* accepting_pollset, AcceptorPtr acceptor,
    const ChannelArgs& args, OrphanablePtr<grpc_endpoint> endpoint)
    : InternallyRefCounted(
          GRPC_TRACE_FLAG_ENABLED(chttp2_server_refcount)
              ? "NewChttp2ServerListener::ActiveConnection::HandshakingState"
              : nullptr),
      connection_(std::move(connection_ref)),
      tcp_server_(tcp_server),
      accepting_pollset_(accepting_pollset),
      acceptor_(std::move(acceptor)),
      interested_parties_(grpc_pollset_set_create()),
      deadline_(GetConnectionDeadline(args)),
      endpoint_(std::move(endpoint)),
      handshake_mgr_(MakeRefCounted<HandshakeManager>()) {
  if (accepting_pollset != nullptr) {
    grpc_pollset_set_add_pollset(interested_parties_, accepting_pollset_);
  }
}

NewChttp2ServerListener::ActiveConnection::HandshakingState::
    ~HandshakingState() {
  if (accepting_pollset_ != nullptr) {
    grpc_pollset_set_del_pollset(interested_parties_, accepting_pollset_);
  }
  grpc_pollset_set_destroy(interested_parties_);
  if (tcp_server_ != nullptr) {
    grpc_tcp_server_unref(tcp_server_);
  }
}

void NewChttp2ServerListener::ActiveConnection::HandshakingState::Orphan() {
  connection_->work_serializer_.Run(
      [this] {
        ShutdownLocked(absl::UnavailableError("Listener stopped serving."));
        Unref();
      },
      DEBUG_LOCATION);
}

void NewChttp2ServerListener::ActiveConnection::HandshakingState::StartLocked(
    const ChannelArgs& channel_args) {
  if (handshake_mgr_ == nullptr) {
    // The connection is already shutting down.
    return;
  }
  CoreConfiguration::Get().handshaker_registry().AddHandshakers(
      HANDSHAKER_SERVER, channel_args, interested_parties_,
      handshake_mgr_.get());
  handshake_mgr_->DoHandshake(
      std::move(endpoint_), channel_args, deadline_, acceptor_.get(),
      [self = Ref()](absl::StatusOr<HandshakerArgs*> result) mutable {
        auto* self_ptr = self.get();
        self_ptr->connection_->work_serializer_.Run(
            [self = std::move(self), result = std::move(result)]() mutable {
              self->OnHandshakeDoneLocked(std::move(result));
            },
            DEBUG_LOCATION);
      });
}

void NewChttp2ServerListener::ActiveConnection::HandshakingState::
    ShutdownLocked(absl::Status status) {
  if (handshake_mgr_ != nullptr) {
    handshake_mgr_->Shutdown(std::move(status));
  }
}

void NewChttp2ServerListener::ActiveConnection::HandshakingState::
    OnTimeoutLocked() {
  if (!timer_handle_.has_value()) {
    return;
  }
  timer_handle_.reset();
  auto t = std::get<RefCountedPtr<grpc_chttp2_transport>>(connection_->state_);
  t->DisconnectWithError(GRPC_ERROR_CREATE(
      "Did not receive HTTP/2 settings before handshake timeout"));
}

void NewChttp2ServerListener::ActiveConnection::HandshakingState::
    OnReceiveSettings(void* arg, grpc_error_handle /* error */) {
  HandshakingState* self = static_cast<HandshakingState*>(arg);
  self->connection_->work_serializer_.Run(
      [self] {
        if (self->timer_handle_.has_value()) {
          self->connection_->listener_state_->event_engine()->Cancel(
              *self->timer_handle_);
          self->timer_handle_.reset();
        }
        self->Unref();
      },
      DEBUG_LOCATION);
}

void NewChttp2ServerListener::ActiveConnection::HandshakingState::
    OnHandshakeDoneLocked(absl::StatusOr<HandshakerArgs*> result) {
  OrphanablePtr<HandshakingState> handshaking_state_ref;
  RefCountedPtr<HandshakeManager> handshake_mgr;
  // If the handshaking succeeded but there is no endpoint, then the
  // handshaker may have handed off the connection to some external
  // code, so we can just clean up here without creating a transport.
  if (!connection_->shutdown_ && result.ok() &&
      (*result)->endpoint != nullptr) {
    RefCountedPtr<Transport> transport =
        grpc_create_chttp2_transport((*result)->args,
                                     std::move((*result)->endpoint), false)
            ->Ref();
    grpc_error_handle channel_init_err =
        connection_->listener_state_->server()->SetupTransport(
            transport.get(), accepting_pollset_, (*result)->args);
    if (channel_init_err.ok()) {
      // Use notify_on_receive_settings callback to enforce the
      // handshake deadline.
      connection_->state_ =
          DownCast<grpc_chttp2_transport*>(transport.get())->Ref();
      Ref().release();  // Held by OnReceiveSettings().
      GRPC_CLOSURE_INIT(&on_receive_settings_, OnReceiveSettings, this,
                        grpc_schedule_on_exec_ctx);
      grpc_closure* on_close = &connection_->on_close_;
      // Refs helds by OnClose()
      connection_->Ref().release();
      grpc_chttp2_transport_start_reading(
          transport.get(), (*result)->read_buffer.c_slice_buffer(),
          &on_receive_settings_, nullptr, on_close);
      timer_handle_ = connection_->listener_state_->event_engine()->RunAfter(
          deadline_ - Timestamp::Now(), [self = Ref()]() mutable {
            // HandshakingState deletion might require an active ExecCtx.
            ExecCtx exec_ctx;
            auto* self_ptr = self.get();
            self_ptr->connection_->work_serializer_.Run(
                [self = std::move(self)]() { self->OnTimeoutLocked(); },
                DEBUG_LOCATION);
          });
    } else {
      // Failed to create channel from transport. Clean up.
      LOG(ERROR) << "Failed to create channel: "
                 << StatusToString(channel_init_err);
      transport->Orphan();
    }
  }
  // Since the handshake manager is done, the connection no longer needs to
  // shutdown the handshake when the listener needs to stop serving.
  handshake_mgr_.reset();
  connection_->listener_state_->OnHandshakeDone(connection_.get());
  // Clean up if we don't have a transport
  if (!std::holds_alternative<RefCountedPtr<grpc_chttp2_transport>>(
          connection_->state_)) {
    connection_->listener_state_->connection_quota()->ReleaseConnections(1);
    connection_->listener_state_->RemoveLogicalConnection(connection_.get());
  }
}

//
// NewChttp2ServerListener::ActiveConnection
//

NewChttp2ServerListener::ActiveConnection::ActiveConnection(
    RefCountedPtr<Server::ListenerState> listener_state,
    grpc_tcp_server* tcp_server, grpc_pollset* accepting_pollset,
    AcceptorPtr acceptor, const ChannelArgs& args, MemoryOwner memory_owner,
    OrphanablePtr<grpc_endpoint> endpoint)
    : LogicalConnection(GRPC_TRACE_FLAG_ENABLED(chttp2_server_refcount)
                            ? "NewChttp2ServerListener::ActiveConnection"
                            : nullptr),
      listener_state_(std::move(listener_state)),
      work_serializer_(
          args.GetObjectRef<grpc_event_engine::experimental::EventEngine>()),
      state_(memory_owner.MakeOrphanable<HandshakingState>(
          RefAsSubclass<ActiveConnection>(), tcp_server, accepting_pollset,
          std::move(acceptor), args, std::move(endpoint))) {
  GRPC_CLOSURE_INIT(&on_close_, ActiveConnection::OnClose, this,
                    grpc_schedule_on_exec_ctx);
}

void NewChttp2ServerListener::ActiveConnection::Orphan() {
  work_serializer_.Run(
      [this]() {
        // If ActiveConnection is orphaned before handshake is established,
        // shutdown the handshaker. If the server is stopping to serve or
        // shutting down and a transport has already been established, GOAWAYs
        // should be sent separately.
        shutdown_ = true;
        if (std::holds_alternative<OrphanablePtr<HandshakingState>>(state_)) {
          state_ = OrphanablePtr<HandshakingState>(nullptr);
        }
        Unref();
      },
      DEBUG_LOCATION);
}

void NewChttp2ServerListener::ActiveConnection::SendGoAway() {
  work_serializer_.Run(
      [self = RefAsSubclass<ActiveConnection>()]() mutable {
        self->SendGoAwayImplLocked();
      },
      DEBUG_LOCATION);
}

void NewChttp2ServerListener::ActiveConnection::DisconnectImmediately() {
  work_serializer_.Run(
      [self = RefAsSubclass<ActiveConnection>()]() mutable {
        self->DisconnectImmediatelyImplLocked();
      },
      DEBUG_LOCATION);
}

void NewChttp2ServerListener::ActiveConnection::Start(const ChannelArgs& args) {
  work_serializer_.Run(
      [self = RefAsSubclass<ActiveConnection>(), args]() mutable {
        // If the Connection is already shutdown at this point, it implies the
        // owning NewChttp2ServerListener and all associated
        // ActiveConnections have been orphaned.
        if (self->shutdown_) return;
        std::get<OrphanablePtr<HandshakingState>>(self->state_)
            ->StartLocked(args);
      },
      DEBUG_LOCATION);
}

void NewChttp2ServerListener::ActiveConnection::OnClose(
    void* arg, grpc_error_handle /* error */) {
  ActiveConnection* self = static_cast<ActiveConnection*>(arg);
  self->listener_state_->RemoveLogicalConnection(self);
  self->listener_state_->connection_quota()->ReleaseConnections(1);
  self->Unref();
}

void NewChttp2ServerListener::ActiveConnection::SendGoAwayImplLocked() {
  if (!shutdown_) {
    shutdown_ = true;
    Match(
        state_,
        [](const OrphanablePtr<HandshakingState>& handshaking_state) {
          // Shutdown the handshaker if it's still in progress.
          if (handshaking_state != nullptr) {
            handshaking_state->ShutdownLocked(
                absl::UnavailableError("Connection going away"));
          }
        },
        [](const RefCountedPtr<grpc_chttp2_transport>& transport) {
          // Send a GOAWAY if the transport exists
          if (transport != nullptr) {
            grpc_transport_op* op = grpc_make_transport_op(nullptr);
            // Set an HTTP2 error of NO_ERROR to do graceful GOAWAYs.
            op->goaway_error = grpc_error_set_int(
                GRPC_ERROR_CREATE("Server is stopping to serve requests."),
                StatusIntProperty::kHttp2Error,
                static_cast<intptr_t>(Http2ErrorCode::kNoError));
            transport->PerformOp(op);
          }
        });
  }
}

void NewChttp2ServerListener::ActiveConnection::
    DisconnectImmediatelyImplLocked() {
  shutdown_ = true;
  Match(
      state_,
      [](const OrphanablePtr<HandshakingState>& handshaking_state) {
        // Shutdown the handshaker if it's still in progress.
        if (handshaking_state != nullptr) {
          handshaking_state->ShutdownLocked(
              absl::UnavailableError("Connection to be disconnected"));
        }
      },
      [](const RefCountedPtr<grpc_chttp2_transport>& transport) {
        // Disconnect immediately if the transport exists
        if (transport != nullptr) {
          grpc_transport_op* op = grpc_make_transport_op(nullptr);
          op->disconnect_with_error = GRPC_ERROR_CREATE(
              "Drain grace time expired. Closing connection immediately.");
          transport->PerformOp(op);
        }
      });
}

//
// NewChttp2ServerListener
//

grpc_error_handle NewChttp2ServerListener::Create(
    Server* server, const EventEngine::ResolvedAddress& addr,
    const ChannelArgs& args, int* port_num) {
  // Create NewChttp2ServerListener.
  OrphanablePtr<NewChttp2ServerListener> listener =
      MakeOrphanable<NewChttp2ServerListener>(args);
  // The tcp_server will be unreffed when the listener is orphaned, which
  // could be at the end of this function if the listener was not added to the
  // server's set of listeners.
  grpc_error_handle error = grpc_tcp_server_create(
      &listener->tcp_server_shutdown_complete_, ChannelArgsEndpointConfig(args),
      OnAccept, listener.get(), &listener->tcp_server_);
  if (!error.ok()) return error;
  // TODO(yijiem): remove this conversion when we remove all
  // grpc_resolved_address usages.
  grpc_resolved_address iomgr_addr =
      grpc_event_engine::experimental::CreateGRPCResolvedAddress(addr);
  if (server->config_fetcher() != nullptr) {
    // TODO(yashykt): Consider binding so as to be able to return the port
    // number.
    listener->resolved_address_ = iomgr_addr;
    {
      MutexLock lock(&listener->mu_);
      listener->add_port_on_start_ = true;
    }
  } else {
    error =
        grpc_tcp_server_add_port(listener->tcp_server_, &iomgr_addr, port_num);
    if (!error.ok()) return error;
  }
  // Create channelz node.
  if (args.GetBool(GRPC_ARG_ENABLE_CHANNELZ)
          .value_or(GRPC_ENABLE_CHANNELZ_DEFAULT)) {
    auto string_address =
        grpc_event_engine::experimental::ResolvedAddressToURI(addr);
    if (!string_address.ok()) {
      return GRPC_ERROR_CREATE(string_address.status().ToString());
    }
    listener->channelz_listen_socket_ =
        MakeRefCounted<channelz::ListenSocketNode>(
            *string_address, absl::StrCat("chttp2 listener ", *string_address));
  }
  // Register with the server only upon success
  server->AddListener(std::move(listener));
  return absl::OkStatus();
}

grpc_error_handle NewChttp2ServerListener::CreateWithAcceptor(
    Server* server, const char* name, const ChannelArgs& args) {
  auto listener = MakeOrphanable<NewChttp2ServerListener>(args);
  grpc_error_handle error = grpc_tcp_server_create(
      &listener->tcp_server_shutdown_complete_, ChannelArgsEndpointConfig(args),
      OnAccept, listener.get(), &listener->tcp_server_);
  if (!error.ok()) return error;
  // TODO(yangg) channelz
  TcpServerFdHandler** arg_val = args.GetPointer<TcpServerFdHandler*>(name);
  *arg_val = grpc_tcp_server_create_fd_handler(listener->tcp_server_);
  server->AddListener(std::move(listener));
  return absl::OkStatus();
}

NewChttp2ServerListener* NewChttp2ServerListener::CreateForPassiveListener(
    Server* server, const ChannelArgs& args,
    std::shared_ptr<experimental::PassiveListenerImpl> passive_listener) {
  // TODO(hork): figure out how to handle channelz in this case
  auto listener = MakeOrphanable<NewChttp2ServerListener>(
      args, std::move(passive_listener));
  auto listener_ptr = listener.get();
  server->AddListener(std::move(listener));
  return listener_ptr;
}

NewChttp2ServerListener::NewChttp2ServerListener(
    const ChannelArgs& args,
    std::shared_ptr<experimental::PassiveListenerImpl> passive_listener)
    : ListenerInterface(GRPC_TRACE_FLAG_ENABLED(chttp2_server_refcount)
                            ? "NewChttp2ServerListener"
                            : nullptr),
      args_(args),
      passive_listener_(std::move(passive_listener)) {
  GRPC_CLOSURE_INIT(&tcp_server_shutdown_complete_, TcpServerShutdownComplete,
                    this, grpc_schedule_on_exec_ctx);
}

NewChttp2ServerListener::~NewChttp2ServerListener() {
  if (passive_listener_ != nullptr) {
    passive_listener_->ListenerDestroyed();
  }
  if (on_destroy_done_ != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, on_destroy_done_, absl::OkStatus());
  }
}

void NewChttp2ServerListener::Start() {
  bool should_add_port = false;
  grpc_tcp_server* tcp_server = nullptr;
  {
    MutexLock lock(&mu_);
    if (!shutdown_) {
      should_add_port = std::exchange(add_port_on_start_, false);
      // Hold a ref while we start the server
      if (tcp_server_ != nullptr) {
        grpc_tcp_server_ref(tcp_server_);
        tcp_server = tcp_server_;
      }
    }
  }
  if (should_add_port) {
    int port_temp;
    grpc_error_handle error =
        grpc_tcp_server_add_port(tcp_server_, resolved_address(), &port_temp);
    if (!error.ok()) {
      LOG(ERROR) << "Error adding port to server: " << StatusToString(error);
      // TODO(yashykt): We wouldn't need to assert here if we bound to the
      // port earlier during AddPort.
      CHECK(0);
    }
  }
  if (tcp_server != nullptr) {
    grpc_tcp_server_start(tcp_server, &listener_state_->server()->pollsets());
    // Give up the ref we took earlier
    grpc_tcp_server_unref(tcp_server);
  }
}

void NewChttp2ServerListener::SetOnDestroyDone(grpc_closure* on_destroy_done) {
  MutexLock lock(&mu_);
  on_destroy_done_ = on_destroy_done;
}

void NewChttp2ServerListener::AcceptConnectedEndpoint(
    std::unique_ptr<EventEngine::Endpoint> endpoint) {
  OnAccept(this, grpc_event_engine_endpoint_create(std::move(endpoint)),
           /*accepting_pollset=*/nullptr, /*acceptor=*/nullptr);
}

void NewChttp2ServerListener::OnAccept(
    void* arg, grpc_endpoint* tcp, grpc_pollset* accepting_pollset,
    grpc_tcp_server_acceptor* server_acceptor) {
  NewChttp2ServerListener* self = static_cast<NewChttp2ServerListener*>(arg);
  OrphanablePtr<grpc_endpoint> endpoint(tcp);
  AcceptorPtr acceptor(server_acceptor);
  if (!self->listener_state_->connection_quota()->AllowIncomingConnection(
          self->listener_state_->memory_quota(),
          grpc_endpoint_get_peer(endpoint.get()))) {
    return;
  }
  {
    // The ref for the tcp_server need to be taken in the critical region
    // after having made sure that the listener has not been Orphaned, so as
    // to avoid heap-use-after-free issues where `Ref()` is invoked when the
    // listener is already shutdown. Note that the listener holds a ref to the
    // tcp_server but this ref is given away when the listener is orphaned
    // (shutdown). A connection needs the tcp_server to outlast the handshake
    // since the acceptor needs it.
    MutexLock lock(&self->mu_);
    if (self->shutdown_) {
      self->listener_state_->connection_quota()->ReleaseConnections(1);
      return;
    }
    if (self->tcp_server_ != nullptr) {
      grpc_tcp_server_ref(self->tcp_server_);
    }
  }
  auto memory_owner =
      self->listener_state_->memory_quota()->CreateMemoryOwner();
  auto connection = memory_owner.MakeOrphanable<ActiveConnection>(
      self->listener_state_, self->tcp_server_, accepting_pollset,
      std::move(acceptor), self->args_, std::move(memory_owner),
      std::move(endpoint));
  RefCountedPtr<ActiveConnection> connection_ref =
      connection->RefAsSubclass<ActiveConnection>();
  std::optional<ChannelArgs> new_args =
      self->listener_state_->AddLogicalConnection(std::move(connection),
                                                  self->args_, tcp);
  if (new_args.has_value()) {
    connection_ref->Start(*new_args);
  } else {
    self->listener_state_->connection_quota()->ReleaseConnections(1);
  }
}

void NewChttp2ServerListener::TcpServerShutdownComplete(
    void* arg, grpc_error_handle /*error*/) {
  NewChttp2ServerListener* self = static_cast<NewChttp2ServerListener*>(arg);
  self->channelz_listen_socket_.reset();
  self->Unref();
}

// Server callback: destroy the tcp listener (so we don't generate further
// callbacks)
void NewChttp2ServerListener::Orphan() {
  grpc_tcp_server* tcp_server;
  {
    MutexLock lock(&mu_);
    shutdown_ = true;
    tcp_server = tcp_server_;
  }
  if (tcp_server != nullptr) {
    grpc_tcp_server_shutdown_listeners(tcp_server);
    grpc_tcp_server_unref(tcp_server);
  } else {
    Unref();
  }
}

absl::StatusOr<int> Chttp2ServerAddPort(Server* server, const char* addr,
                                        const ChannelArgs& args) {
  if (addr == nullptr) {
    return GRPC_ERROR_CREATE("Invalid address: addr cannot be a nullptr.");
  }
  if (strncmp(addr, "external:", 9) == 0) {
    auto r = NewChttp2ServerListener::CreateWithAcceptor(server, addr, args);
    if (!r.ok()) return r;
    return -1;
  }
  int port_num = -1;
  absl::StatusOr<std::vector<grpc_resolved_address>> resolved;
  absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> results =
      std::vector<EventEngine::ResolvedAddress>();
  std::vector<grpc_error_handle> error_list;
  std::string parsed_addr = URI::PercentDecode(addr);
  absl::string_view parsed_addr_unprefixed{parsed_addr};
  // Using lambda to avoid use of goto.
  grpc_error_handle error = [&]() {
    grpc_error_handle error;
    // TODO(ladynana, yijiem): this code does not handle address URIs correctly:
    // it's parsing `unix://foo/bar` as path `/foo/bar` when it should be
    // parsing it as authority `foo` and path `/bar`. Also add API documentation
    // on the valid URIs that grpc_server_add_http2_port accepts.
    if (absl::ConsumePrefix(&parsed_addr_unprefixed, kUnixUriPrefix)) {
      resolved = grpc_resolve_unix_domain_address(parsed_addr_unprefixed);
      GRPC_RETURN_IF_ERROR(resolved.status());
    } else if (absl::ConsumePrefix(&parsed_addr_unprefixed,
                                   kUnixAbstractUriPrefix)) {
      resolved =
          grpc_resolve_unix_abstract_domain_address(parsed_addr_unprefixed);
      GRPC_RETURN_IF_ERROR(resolved.status());
    } else if (absl::ConsumePrefix(&parsed_addr_unprefixed, kVSockUriPrefix)) {
      resolved = grpc_resolve_vsock_address(parsed_addr_unprefixed);
      GRPC_RETURN_IF_ERROR(resolved.status());
    } else {
      if (IsEventEngineDnsNonClientChannelEnabled()) {
        absl::StatusOr<std::unique_ptr<EventEngine::DNSResolver>> ee_resolver =
            args.GetObjectRef<EventEngine>()->GetDNSResolver(
                EventEngine::DNSResolver::ResolverOptions());
        GRPC_RETURN_IF_ERROR(ee_resolver.status());
        results = grpc_event_engine::experimental::LookupHostnameBlocking(
            ee_resolver->get(), parsed_addr, "https");
      } else {
        // TODO(yijiem): Remove this after event_engine_dns_non_client_channel
        // is fully enabled.
        absl::StatusOr<std::vector<grpc_resolved_address>> iomgr_results =
            GetDNSResolver()->LookupHostnameBlocking(parsed_addr, "https");
        GRPC_RETURN_IF_ERROR(iomgr_results.status());
        for (const auto& addr : *iomgr_results) {
          results->push_back(
              grpc_event_engine::experimental::CreateResolvedAddress(addr));
        }
      }
    }
    if (resolved.ok()) {
      for (const auto& addr : *resolved) {
        results->push_back(
            grpc_event_engine::experimental::CreateResolvedAddress(addr));
      }
    }
    GRPC_RETURN_IF_ERROR(results.status());
    // Create a listener for each resolved address.
    for (EventEngine::ResolvedAddress& addr : *results) {
      // If address has a wildcard port (0), use the same port as a previous
      // listener.
      if (port_num != -1 &&
          grpc_event_engine::experimental::ResolvedAddressGetPort(addr) == 0) {
        grpc_event_engine::experimental::ResolvedAddressSetPort(addr, port_num);
      }
      int port_temp = -1;
      error = NewChttp2ServerListener::Create(server, addr, args, &port_temp);
      if (!error.ok()) {
        error_list.push_back(error);
      } else {
        if (port_num == -1) {
          port_num = port_temp;
        } else {
          CHECK(port_num == port_temp);
        }
      }
    }
    if (error_list.size() == results->size()) {
      std::string msg = absl::StrFormat(
          "No address added out of total %" PRIuPTR " resolved for '%s'",
          results->size(), addr);
      return GRPC_ERROR_CREATE_REFERENCING(msg.c_str(), error_list.data(),
                                           error_list.size());
    } else if (!error_list.empty()) {
      std::string msg =
          absl::StrFormat("Only %" PRIuPTR
                          " addresses added out of total %" PRIuPTR " resolved",
                          results->size() - error_list.size(), results->size());
      error = GRPC_ERROR_CREATE_REFERENCING(msg.c_str(), error_list.data(),
                                            error_list.size());
      LOG(INFO) << "WARNING: " << StatusToString(error);
      // we managed to bind some addresses: continue without error
    }
    return absl::OkStatus();
  }();  // lambda end
  if (!error.ok()) return error;
  return port_num;
}

namespace experimental {

absl::Status PassiveListenerImpl::AcceptConnectedEndpoint(
    std::unique_ptr<EventEngine::Endpoint> endpoint) {
  CHECK_NE(server_.get(), nullptr);
  RefCountedPtr<NewChttp2ServerListener> new_listener;
  {
    MutexLock lock(&mu_);
    auto* new_listener_ptr = std::get_if<NewChttp2ServerListener*>(&listener_);
    if (new_listener_ptr != nullptr && *new_listener_ptr != nullptr) {
      new_listener = (*new_listener_ptr)
                         ->RefIfNonZero()
                         .TakeAsSubclass<NewChttp2ServerListener>();
    }
  }
  if (new_listener == nullptr) {
    return absl::UnavailableError("passive listener already shut down");
  }
  ExecCtx exec_ctx;
  new_listener->AcceptConnectedEndpoint(std::move(endpoint));
  return absl::OkStatus();
}

absl::Status PassiveListenerImpl::AcceptConnectedFd(int fd) {
  CHECK_NE(server_.get(), nullptr);
  ExecCtx exec_ctx;
  auto& args = server_->channel_args();
  auto* supports_fd = QueryExtension<EventEngineSupportsFdExtension>(
      args.GetObjectRef<EventEngine>().get());
  if (supports_fd == nullptr) {
    return absl::UnimplementedError(
        "The server's EventEngine does not support adding endpoints from "
        "connected file descriptors.");
  }
  auto endpoint =
      supports_fd->CreateEndpointFromFd(fd, ChannelArgsEndpointConfig(args));
  return AcceptConnectedEndpoint(std::move(endpoint));
}

void PassiveListenerImpl::ListenerDestroyed() {
  MutexLock lock(&mu_);
  listener_ = static_cast<Chttp2ServerListener*>(nullptr);
}

}  // namespace experimental
}  // namespace grpc_core

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD
void grpc_server_add_channel_from_fd(grpc_server* server, int fd,
                                     grpc_server_credentials* creds) {
  // For now, we only support insecure server credentials
  if (creds == nullptr ||
      creds->type() != grpc_core::InsecureServerCredentials::Type()) {
    LOG(ERROR) << "Failed to create channel due to invalid creds";
    return;
  }
  grpc_core::ExecCtx exec_ctx;
  grpc_core::Server* core_server = grpc_core::Server::FromC(server);

  grpc_core::ChannelArgs server_args = core_server->channel_args();
  std::string name = absl::StrCat("fd:", fd);
  auto memory_quota =
      server_args.GetObject<grpc_core::ResourceQuota>()->memory_quota();
  grpc_core::OrphanablePtr<grpc_endpoint> server_endpoint(
      grpc_tcp_create_from_fd(
          grpc_fd_create(fd, name.c_str(), true),
          grpc_event_engine::experimental::ChannelArgsEndpointConfig(
              server_args),
          name));
  for (grpc_pollset* pollset : core_server->pollsets()) {
    grpc_endpoint_add_to_pollset(server_endpoint.get(), pollset);
  }
  grpc_core::Transport* transport = grpc_create_chttp2_transport(
      server_args, std::move(server_endpoint), false  // is_client
  );
  grpc_error_handle error =
      core_server->SetupTransport(transport, nullptr, server_args);
  if (error.ok()) {
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr,
                                        nullptr);
  } else {
    LOG(ERROR) << "Failed to create channel: "
               << grpc_core::StatusToString(error);
    transport->Orphan();
  }
}

#else  // !GPR_SUPPORT_CHANNELS_FROM_FD

void grpc_server_add_channel_from_fd(grpc_server* /* server */, int /* fd */,
                                     grpc_server_credentials* /* creds */) {
  CHECK(0);
}

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD

absl::Status grpc_server_add_passive_listener(
    grpc_core::Server* server, grpc_server_credentials* credentials,
    std::shared_ptr<grpc_core::experimental::PassiveListenerImpl>
        passive_listener) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_server_add_passive_listener(server=" << server
      << ", credentials=" << credentials << ")";
  // Create security context.
  if (credentials == nullptr) {
    return absl::UnavailableError(
        "No credentials specified for passive listener");
  }
  auto sc = credentials->create_security_connector(grpc_core::ChannelArgs());
  if (sc == nullptr) {
    return absl::UnavailableError(
        absl::StrCat("Unable to create secure server with credentials of type ",
                     credentials->type().name()));
  }
  auto args = server->channel_args()
                  .SetObject(credentials->Ref())
                  .SetObject(std::move(sc));
  passive_listener->listener_ =
      grpc_core::NewChttp2ServerListener::CreateForPassiveListener(
          server, args, passive_listener);

  passive_listener->server_ = server->Ref();
  return absl::OkStatus();
}
