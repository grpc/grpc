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

#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <memory>
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
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/passive_listener.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>

#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/legacy_frame.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/handshaker/handshaker_registry.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/extensions/supports_fd.h"
#include "src/core/lib/event_engine/query_extensions.h"
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
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/insecure/insecure_credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/server/server.h"
#include "src/core/util/debug_location.h"
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

const char kUnixUriPrefix[] = "unix:";
const char kUnixAbstractUriPrefix[] = "unix-abstract:";
const char kVSockUriPrefix[] = "vsock:";

struct AcceptorDeleter {
  void operator()(grpc_tcp_server_acceptor* acceptor) const {
    gpr_free(acceptor);
  }
};
using AcceptorPtr = std::unique_ptr<grpc_tcp_server_acceptor, AcceptorDeleter>;

class Chttp2ServerListener : public Server::ListenerInterface {
 public:
  static grpc_error_handle Create(Server* server, grpc_resolved_address* addr,
                                  const ChannelArgs& args, int* port_num);

  static grpc_error_handle CreateWithAcceptor(Server* server, const char* name,
                                              const ChannelArgs& args);

  static Chttp2ServerListener* CreateForPassiveListener(
      Server* server, const ChannelArgs& args,
      std::shared_ptr<experimental::PassiveListenerImpl> passive_listener);

  // Do not instantiate directly.  Use one of the factory methods above.
  Chttp2ServerListener(Server* server, const ChannelArgs& args,
                       std::shared_ptr<experimental::PassiveListenerImpl>
                           passive_listener = nullptr);
  ~Chttp2ServerListener() override;

  void AcceptConnectedEndpoint(std::unique_ptr<EventEngine::Endpoint> endpoint);

  channelz::ListenSocketNode* channelz_listen_socket_node() const override {
    return channelz_listen_socket_.get();
  }

  void SetOnDestroyDone(grpc_closure* on_destroy_done) override;

 private:
  friend class experimental::PassiveListenerImpl;

  class ActiveConnection : public LogicalConnection {
   public:
    class HandshakingState : public InternallyRefCounted<HandshakingState> {
     public:
      HandshakingState(RefCountedPtr<ActiveConnection> connection_ref,
                       grpc_pollset* accepting_pollset, AcceptorPtr acceptor,
                       const ChannelArgs& args);

      ~HandshakingState() override;

      void Orphan() override;

      void Start(OrphanablePtr<grpc_endpoint> endpoint,
                 const ChannelArgs& args);

      void ShutdownLocked(absl::Status status)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ActiveConnection::mu_);

      // Needed to be able to grab an external ref in
      // ActiveConnection::Start()
      using InternallyRefCounted<HandshakingState>::Ref;

     private:
      void OnTimeout() ABSL_LOCKS_EXCLUDED(&ActiveConnection::mu_);
      static void OnReceiveSettings(void* arg, grpc_error_handle /* error */);
      void OnHandshakeDone(absl::StatusOr<HandshakerArgs*> result);
      RefCountedPtr<ActiveConnection> const connection_;
      grpc_pollset* const accepting_pollset_;
      AcceptorPtr acceptor_;
      RefCountedPtr<HandshakeManager> handshake_mgr_
          ABSL_GUARDED_BY(&ActiveConnection::mu_);
      // State for enforcing handshake timeout on receiving HTTP/2 settings.
      Timestamp const deadline_;
      absl::optional<EventEngine::TaskHandle> timer_handle_
          ABSL_GUARDED_BY(&ActiveConnection::mu_);
      grpc_closure on_receive_settings_ ABSL_GUARDED_BY(&ActiveConnection::mu_);
      grpc_pollset_set* const interested_parties_;
    };

    ActiveConnection(RefCountedPtr<Chttp2ServerListener> listener,
                     grpc_pollset* accepting_pollset, AcceptorPtr acceptor,
                     const ChannelArgs& args, MemoryOwner memory_owner);
    void Start(OrphanablePtr<grpc_endpoint> endpoint, const ChannelArgs& args);

    void Orphan() override;

    // Needed to be able to grab an external weak ref in
    // Chttp2ServerListener::OnAccept()
    using InternallyRefCounted<LogicalConnection>::RefAsSubclass;

   private:
    bool SendGoAwayImpl() override;
    void DisconnectImmediatelyImpl() override;
    static void OnClose(void* arg, grpc_error_handle error);
    void OnDrainGraceTimeExpiry() ABSL_LOCKS_EXCLUDED(&mu_);

    Chttp2ServerListener* listener_as_subclass() const {
      return DownCast<Chttp2ServerListener*>(listener().get());
    }
    Mutex mu_ /*ABSL_ACQUIRED_AFTER(&ListenerInterface::mu_)*/;
    // Set by HandshakingState before the handshaking begins and reset when
    // handshaking is done.
    OrphanablePtr<HandshakingState> handshaking_state_ ABSL_GUARDED_BY(&mu_);
    // Set by HandshakingState when handshaking is done and a valid transport
    // is created.
    RefCountedPtr<grpc_chttp2_transport> transport_ ABSL_GUARDED_BY(&mu_) =
        nullptr;
    grpc_closure on_close_;
    bool shutdown_ ABSL_GUARDED_BY(&mu_) = false;
  };

  // To allow access to RefCounted<> like interface.
  friend class RefCountedPtr<Chttp2ServerListener>;

  // Should only be called once so as to start the TCP server. This should only
  // be called by the config fetcher.
  void StartImpl() override;

  static void OnAccept(void* arg, grpc_endpoint* tcp,
                       grpc_pollset* accepting_pollset,
                       grpc_tcp_server_acceptor* acceptor);

  static void TcpServerShutdownComplete(void* arg, grpc_error_handle error);

  static void DestroyListener(Server* /*server*/, void* arg,
                              grpc_closure* destroy_done);

  void OrphanImpl() override;

  grpc_tcp_server* tcp_server_ = nullptr;
  ChannelArgs args_;
  Mutex mu_;
  bool add_port_on_start_ ABSL_GUARDED_BY(mu_) = false;
  // Signals whether the application has triggered shutdown.
  bool shutdown_ ABSL_GUARDED_BY(mu_) = false;
  grpc_closure tcp_server_shutdown_complete_ ABSL_GUARDED_BY(mu_);
  grpc_closure* on_destroy_done_ ABSL_GUARDED_BY(mu_) = nullptr;
  RefCountedPtr<channelz::ListenSocketNode> channelz_listen_socket_;
  MemoryQuotaRefPtr memory_quota_;
  ConnectionQuotaRefPtr connection_quota_;
  // TODO(yashykt): consider using absl::variant<> to minimize memory usage for
  // disjoint cases where different fields are used.
  std::shared_ptr<experimental::PassiveListenerImpl> passive_listener_;
};

//
// Chttp2ServerListener::ActiveConnection::HandshakingState
//

Timestamp GetConnectionDeadline(const ChannelArgs& args) {
  return Timestamp::Now() +
         std::max(
             Duration::Milliseconds(1),
             args.GetDurationFromIntMillis(GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS)
                 .value_or(Duration::Seconds(120)));
}

Chttp2ServerListener::ActiveConnection::HandshakingState::HandshakingState(
    RefCountedPtr<ActiveConnection> connection_ref,
    grpc_pollset* accepting_pollset, AcceptorPtr acceptor,
    const ChannelArgs& args)
    : connection_(std::move(connection_ref)),
      accepting_pollset_(accepting_pollset),
      acceptor_(std::move(acceptor)),
      handshake_mgr_(MakeRefCounted<HandshakeManager>()),
      deadline_(GetConnectionDeadline(args)),
      interested_parties_(grpc_pollset_set_create()) {
  if (accepting_pollset != nullptr) {
    grpc_pollset_set_add_pollset(interested_parties_, accepting_pollset_);
  }
  CoreConfiguration::Get().handshaker_registry().AddHandshakers(
      HANDSHAKER_SERVER, args, interested_parties_, handshake_mgr_.get());
}

Chttp2ServerListener::ActiveConnection::HandshakingState::~HandshakingState() {
  if (accepting_pollset_ != nullptr) {
    grpc_pollset_set_del_pollset(interested_parties_, accepting_pollset_);
  }
  grpc_pollset_set_destroy(interested_parties_);
  if (connection_->listener() != nullptr &&
      connection_->listener_as_subclass()->tcp_server_ != nullptr) {
    grpc_tcp_server_unref(connection_->listener_as_subclass()->tcp_server_);
  }
}

void Chttp2ServerListener::ActiveConnection::HandshakingState::Orphan() {
  {
    MutexLock lock(&connection_->mu_);
    ShutdownLocked(absl::UnavailableError("Listener stopped serving."));
  }
  Unref();
}

void Chttp2ServerListener::ActiveConnection::HandshakingState::Start(
    OrphanablePtr<grpc_endpoint> endpoint, const ChannelArgs& channel_args) {
  RefCountedPtr<HandshakeManager> handshake_mgr;
  {
    MutexLock lock(&connection_->mu_);
    if (handshake_mgr_ == nullptr) return;
    handshake_mgr = handshake_mgr_;
  }
  handshake_mgr->DoHandshake(
      std::move(endpoint), channel_args, deadline_, acceptor_.get(),
      [self = Ref()](absl::StatusOr<HandshakerArgs*> result) {
        self->OnHandshakeDone(std::move(result));
      });
}

void Chttp2ServerListener::ActiveConnection::HandshakingState::ShutdownLocked(
    absl::Status status) {
  if (handshake_mgr_ != nullptr) {
    handshake_mgr_->Shutdown(std::move(status));
  }
}

void Chttp2ServerListener::ActiveConnection::HandshakingState::OnTimeout() {
  grpc_chttp2_transport* transport = nullptr;
  {
    MutexLock lock(&connection_->mu_);
    if (timer_handle_.has_value()) {
      transport = connection_->transport_.get();
      timer_handle_.reset();
    }
  }
  if (transport != nullptr) {
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->disconnect_with_error = GRPC_ERROR_CREATE(
        "Did not receive HTTP/2 settings before handshake timeout");
    transport->PerformOp(op);
  }
}

void Chttp2ServerListener::ActiveConnection::HandshakingState::
    OnReceiveSettings(void* arg, grpc_error_handle /* error */) {
  HandshakingState* self = static_cast<HandshakingState*>(arg);
  {
    MutexLock lock(&self->connection_->mu_);
    if (self->timer_handle_.has_value()) {
      self->connection_->event_engine()->Cancel(*self->timer_handle_);
      self->timer_handle_.reset();
    }
  }
  self->Unref();
}

void Chttp2ServerListener::ActiveConnection::HandshakingState::OnHandshakeDone(
    absl::StatusOr<HandshakerArgs*> result) {
  OrphanablePtr<HandshakingState> handshaking_state_ref;
  RefCountedPtr<HandshakeManager> handshake_mgr;
  bool cleanup_connection = false;
  bool release_connection = false;
  {
    MutexLock connection_lock(&connection_->mu_);
    if (!result.ok() || connection_->shutdown_) {
      cleanup_connection = true;
      release_connection = true;
    } else {
      // If the handshaking succeeded but there is no endpoint, then the
      // handshaker may have handed off the connection to some external
      // code, so we can just clean up here without creating a transport.
      if ((*result)->endpoint != nullptr) {
        RefCountedPtr<Transport> transport =
            grpc_create_chttp2_transport((*result)->args,
                                         std::move((*result)->endpoint), false)
                ->Ref();
        grpc_error_handle channel_init_err =
            connection_->listener()->server()->SetupTransport(
                transport.get(), accepting_pollset_, (*result)->args,
                grpc_chttp2_transport_get_socket_node(transport.get()));
        if (channel_init_err.ok()) {
          // Use notify_on_receive_settings callback to enforce the
          // handshake deadline.
          connection_->transport_ =
              DownCast<grpc_chttp2_transport*>(transport.get())->Ref();
          Ref().release();  // Held by OnReceiveSettings().
          GRPC_CLOSURE_INIT(&on_receive_settings_, OnReceiveSettings, this,
                            grpc_schedule_on_exec_ctx);
          // If the listener has been configured with a config fetcher, we
          // need to watch on the transport being closed so that we can update
          // the list of active connections.
          grpc_closure* on_close = nullptr;
          if (connection_->listener()->server()->config_fetcher() != nullptr) {
            // Refs helds by OnClose()
            connection_->Ref().release();
            on_close = &connection_->on_close_;
          } else {
            // Remove the connection from the connections_ map since OnClose()
            // will not be invoked when a config fetcher is not set.
            auto connection_quota = connection_->listener_as_subclass()
                                        ->connection_quota_->Ref()
                                        .release();
            auto on_close_transport = [](void* arg,
                                         grpc_error_handle /*handle*/) {
              ConnectionQuota* connection_quota =
                  static_cast<ConnectionQuota*>(arg);
              connection_quota->ReleaseConnections(1);
              connection_quota->Unref();
            };
            on_close = GRPC_CLOSURE_CREATE(on_close_transport, connection_quota,
                                           grpc_schedule_on_exec_ctx_);
            cleanup_connection = true;
          }
          grpc_chttp2_transport_start_reading(
              transport.get(), (*result)->read_buffer.c_slice_buffer(),
              &on_receive_settings_, nullptr, on_close);
          timer_handle_ = connection_->event_engine()->RunAfter(
              deadline_ - Timestamp::Now(), [self = Ref()]() mutable {
                ApplicationCallbackExecCtx callback_exec_ctx;
                ExecCtx exec_ctx;
                self->OnTimeout();
                // HandshakingState deletion might require an active ExecCtx.
                self.reset();
              });
        } else {
          // Failed to create channel from transport. Clean up.
          LOG(ERROR) << "Failed to create channel: "
                     << StatusToString(channel_init_err);
          transport->Orphan();
          cleanup_connection = true;
          release_connection = true;
        }
      } else {
        cleanup_connection = true;
        release_connection = true;
      }
    }
    // Since the handshake manager is done, the connection no longer needs to
    // shutdown the handshake when the listener needs to stop serving.
    // Avoid calling the destructor of HandshakeManager and HandshakingState
    // from within the critical region.
    handshake_mgr = std::move(handshake_mgr_);
    handshaking_state_ref = std::move(connection_->handshaking_state_);
  }
  if (cleanup_connection) {
    if (release_connection) {
      connection_->listener_as_subclass()
          ->connection_quota_->ReleaseConnections(1);
    }
    connection_->listener_as_subclass()->RemoveLogicalConnection(
        connection_.get());
  }
}

//
// Chttp2ServerListener::ActiveConnection
//

Chttp2ServerListener::ActiveConnection::ActiveConnection(
    RefCountedPtr<Chttp2ServerListener> listener,
    grpc_pollset* accepting_pollset, AcceptorPtr acceptor,
    const ChannelArgs& args, MemoryOwner memory_owner)
    : LogicalConnection(std::move(listener)),
      handshaking_state_(memory_owner.MakeOrphanable<HandshakingState>(
          RefAsSubclass<ActiveConnection>(), accepting_pollset,
          std::move(acceptor), args)) {
  GRPC_CLOSURE_INIT(&on_close_, ActiveConnection::OnClose, this,
                    grpc_schedule_on_exec_ctx);
}

void Chttp2ServerListener::ActiveConnection::Orphan() {
  OrphanablePtr<HandshakingState> handshaking_state;
  {
    MutexLock lock(&mu_);
    shutdown_ = true;
    // Reset handshaking_state_ since we have been orphaned by the listener
    // signaling that the listener has stopped serving.
    handshaking_state = std::move(handshaking_state_);
  }
  Unref();
}

bool Chttp2ServerListener::ActiveConnection::SendGoAwayImpl() {
  grpc_chttp2_transport* transport = nullptr;
  {
    MutexLock lock(&mu_);
    if (!shutdown_) {
      // Send a GOAWAY if the transport exists
      if (transport_ != nullptr) {
        transport = transport_.get();
      }
      // Shutdown the handshaker if it's still in progress.
      if (handshaking_state_ != nullptr) {
        handshaking_state_->ShutdownLocked(
            absl::UnavailableError("Connection going away"));
      }
      shutdown_ = true;
    }
  }
  if (transport != nullptr) {
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->goaway_error =
        GRPC_ERROR_CREATE("Server is stopping to serve requests.");
    transport->PerformOp(op);
    return true;
  }
  return false;
}

void Chttp2ServerListener::ActiveConnection::Start(
    OrphanablePtr<grpc_endpoint> endpoint, const ChannelArgs& args) {
  RefCountedPtr<HandshakingState> handshaking_state_ref;
  {
    MutexLock lock(&mu_);
    // If the Connection is already shutdown at this point, it implies the
    // owning Chttp2ServerListener and all associated ActiveConnections have
    // been orphaned.
    if (shutdown_) return;
    // Hold a ref to HandshakingState to allow starting the handshake outside
    // the critical region.
    handshaking_state_ref = handshaking_state_->Ref();
  }
  handshaking_state_ref->Start(std::move(endpoint), args);
}

void Chttp2ServerListener::ActiveConnection::OnClose(
    void* arg, grpc_error_handle /* error */) {
  ActiveConnection* self = static_cast<ActiveConnection*>(arg);
  {
    MutexLock lock(&self->mu_);
    self->shutdown_ = true;
  }
  self->listener_as_subclass()->RemoveLogicalConnection(self);
  self->listener_as_subclass()->connection_quota_->ReleaseConnections(1);
  self->Unref();
}

void Chttp2ServerListener::ActiveConnection::DisconnectImmediatelyImpl() {
  grpc_chttp2_transport* transport = nullptr;
  // If the drain_grace_timer_ was not cancelled, disconnect the transport
  // immediately.
  {
    MutexLock lock(&mu_);
    transport = transport_.get();
  }
  if (transport != nullptr) {
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->disconnect_with_error = GRPC_ERROR_CREATE(
        "Drain grace time expired. Closing connection immediately.");
    transport->PerformOp(op);
  }
}

//
// Chttp2ServerListener
//

grpc_error_handle Chttp2ServerListener::Create(Server* server,
                                               grpc_resolved_address* addr,
                                               const ChannelArgs& args,
                                               int* port_num) {
  // Create Chttp2ServerListener.
  OrphanablePtr<Chttp2ServerListener> listener =
      MakeOrphanable<Chttp2ServerListener>(server, args);
  // The tcp_server will be unreffed when the listener is orphaned, which could
  // be at the end of this function if the listener was not added to the
  // server's set of listeners.
  grpc_error_handle error = grpc_tcp_server_create(
      &listener->tcp_server_shutdown_complete_, ChannelArgsEndpointConfig(args),
      OnAccept, listener.get(), &listener->tcp_server_);
  if (!error.ok()) return error;
  if (listener->server()->config_fetcher() != nullptr) {
    // TODO(yashykt): Consider binding so as to be able to return the port
    // number.
    listener->set_resolved_address(*addr);
    {
      MutexLock lock(&listener->mu_);
      listener->add_port_on_start_ = true;
    }
  } else {
    error = grpc_tcp_server_add_port(listener->tcp_server_, addr, port_num);
    if (!error.ok()) return error;
  }
  // Create channelz node.
  if (args.GetBool(GRPC_ARG_ENABLE_CHANNELZ)
          .value_or(GRPC_ENABLE_CHANNELZ_DEFAULT)) {
    auto string_address = grpc_sockaddr_to_uri(addr);
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

grpc_error_handle Chttp2ServerListener::CreateWithAcceptor(
    Server* server, const char* name, const ChannelArgs& args) {
  auto listener = MakeOrphanable<Chttp2ServerListener>(server, args);
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

Chttp2ServerListener* Chttp2ServerListener::CreateForPassiveListener(
    Server* server, const ChannelArgs& args,
    std::shared_ptr<experimental::PassiveListenerImpl> passive_listener) {
  // TODO(hork): figure out how to handle channelz in this case
  auto listener = MakeOrphanable<Chttp2ServerListener>(
      server, args, std::move(passive_listener));
  auto listener_ptr = listener.get();
  server->AddListener(std::move(listener));
  return listener_ptr;
}

Chttp2ServerListener::Chttp2ServerListener(
    Server* server, const ChannelArgs& args,
    std::shared_ptr<experimental::PassiveListenerImpl> passive_listener)
    : ListenerInterface(server),
      args_(args),
      memory_quota_(args.GetObject<ResourceQuota>()->memory_quota()),
      connection_quota_(MakeRefCounted<ConnectionQuota>()),
      passive_listener_(std::move(passive_listener)) {
  auto max_allowed_incoming_connections =
      args.GetInt(GRPC_ARG_MAX_ALLOWED_INCOMING_CONNECTIONS);
  if (max_allowed_incoming_connections.has_value()) {
    connection_quota_->SetMaxIncomingConnections(
        max_allowed_incoming_connections.value());
  }
  GRPC_CLOSURE_INIT(&tcp_server_shutdown_complete_, TcpServerShutdownComplete,
                    this, grpc_schedule_on_exec_ctx);
}

Chttp2ServerListener::~Chttp2ServerListener() {
  // Flush queued work before destroying handshaker factory, since that
  // may do a synchronous unref.
  ExecCtx::Get()->Flush();
  if (passive_listener_ != nullptr) {
    passive_listener_->ListenerDestroyed();
  }
  if (on_destroy_done_ != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, on_destroy_done_, absl::OkStatus());
    ExecCtx::Get()->Flush();
  }
}

void Chttp2ServerListener::StartImpl() {
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
    grpc_tcp_server_start(tcp_server, &server()->pollsets());
    // Give up the ref we took earlier
    grpc_tcp_server_unref(tcp_server);
  }
}

void Chttp2ServerListener::SetOnDestroyDone(grpc_closure* on_destroy_done) {
  MutexLock lock(&mu_);
  on_destroy_done_ = on_destroy_done;
}

void Chttp2ServerListener::AcceptConnectedEndpoint(
    std::unique_ptr<EventEngine::Endpoint> endpoint) {
  OnAccept(this, grpc_event_engine_endpoint_create(std::move(endpoint)),
           /*accepting_pollset=*/nullptr, /*acceptor=*/nullptr);
}

void Chttp2ServerListener::OnAccept(void* arg, grpc_endpoint* tcp,
                                    grpc_pollset* accepting_pollset,
                                    grpc_tcp_server_acceptor* server_acceptor) {
  Chttp2ServerListener* self = static_cast<Chttp2ServerListener*>(arg);
  OrphanablePtr<grpc_endpoint> endpoint(tcp);
  AcceptorPtr acceptor(server_acceptor);
  if (!self->connection_quota_->AllowIncomingConnection(
          self->memory_quota_, grpc_endpoint_get_peer(endpoint.get()))) {
    return;
  }
  RefCountedPtr<ActiveConnection> connection_ref;
  RefCountedPtr<Chttp2ServerListener> listener_ref;
  ChannelArgs new_args;
  self->AddLogicalConnection(
      [&](const ChannelArgs& args) -> OrphanablePtr<LogicalConnection> {
        {
          // The ref for both the listener and tcp_server need to be taken in
          // the critical region after having made sure that the listener has
          // not been Orphaned, so as to avoid heap-use-after-free issues where
          // `Ref()` is invoked when the listener is already shutdown. Note that
          // the listener holds a ref to the tcp_server but this ref is given
          // away when the listener is orphaned (shutdown). A connection needs
          // the tcp_server to outlast the handshake since the acceptor needs
          // it.
          MutexLock lock(&self->mu_);
          if (self->shutdown_) {
            return nullptr;
          }
          if (self->tcp_server_ != nullptr) {
            grpc_tcp_server_ref(self->tcp_server_);
          }
          listener_ref = self->RefAsSubclass<Chttp2ServerListener>();
        }
        auto memory_owner = self->memory_quota_->CreateMemoryOwner();
        auto connection = memory_owner.MakeOrphanable<ActiveConnection>(
            std::move(listener_ref), accepting_pollset, std::move(acceptor),
            args, std::move(memory_owner));
        connection_ref = connection->RefAsSubclass<ActiveConnection>();
        new_args = args;
        return connection;
      },
      self->args_, tcp);
  if (connection_ref != nullptr) {
    connection_ref->Start(std::move(endpoint), new_args);
  }
}

void Chttp2ServerListener::TcpServerShutdownComplete(
    void* arg, grpc_error_handle /*error*/) {
  Chttp2ServerListener* self = static_cast<Chttp2ServerListener*>(arg);
  self->channelz_listen_socket_.reset();
  self->Unref();
}

// Server callback: destroy the tcp listener (so we don't generate further
// callbacks)
void Chttp2ServerListener::OrphanImpl() {
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

namespace {

grpc_error_handle Chttp2ServerAddPort(Server* server, const char* addr,
                                      const ChannelArgs& args, int* port_num) {
  if (addr == nullptr) {
    return GRPC_ERROR_CREATE("Invalid address: addr cannot be a nullptr.");
  }
  if (strncmp(addr, "external:", 9) == 0) {
    return Chttp2ServerListener::CreateWithAcceptor(server, addr, args);
  }
  *port_num = -1;
  absl::StatusOr<std::vector<grpc_resolved_address>> resolved_or;
  std::vector<grpc_error_handle> error_list;
  std::string parsed_addr = URI::PercentDecode(addr);
  absl::string_view parsed_addr_unprefixed{parsed_addr};
  // Using lambda to avoid use of goto.
  grpc_error_handle error = [&]() {
    grpc_error_handle error;
    if (absl::ConsumePrefix(&parsed_addr_unprefixed, kUnixUriPrefix)) {
      resolved_or = grpc_resolve_unix_domain_address(parsed_addr_unprefixed);
    } else if (absl::ConsumePrefix(&parsed_addr_unprefixed,
                                   kUnixAbstractUriPrefix)) {
      resolved_or =
          grpc_resolve_unix_abstract_domain_address(parsed_addr_unprefixed);
    } else if (absl::ConsumePrefix(&parsed_addr_unprefixed, kVSockUriPrefix)) {
      resolved_or = grpc_resolve_vsock_address(parsed_addr_unprefixed);
    } else {
      resolved_or =
          GetDNSResolver()->LookupHostnameBlocking(parsed_addr, "https");
    }
    if (!resolved_or.ok()) {
      return absl_status_to_grpc_error(resolved_or.status());
    }
    // Create a listener for each resolved address.
    for (auto& addr : *resolved_or) {
      // If address has a wildcard port (0), use the same port as a previous
      // listener.
      if (*port_num != -1 && grpc_sockaddr_get_port(&addr) == 0) {
        grpc_sockaddr_set_port(&addr, *port_num);
      }
      int port_temp = -1;
      error = Chttp2ServerListener::Create(server, &addr, args, &port_temp);
      if (!error.ok()) {
        error_list.push_back(error);
      } else {
        if (*port_num == -1) {
          *port_num = port_temp;
        } else {
          CHECK(*port_num == port_temp);
        }
      }
    }
    if (error_list.size() == resolved_or->size()) {
      std::string msg = absl::StrFormat(
          "No address added out of total %" PRIuPTR " resolved for '%s'",
          resolved_or->size(), addr);
      return GRPC_ERROR_CREATE_REFERENCING(msg.c_str(), error_list.data(),
                                           error_list.size());
    } else if (!error_list.empty()) {
      std::string msg = absl::StrFormat(
          "Only %" PRIuPTR " addresses added out of total %" PRIuPTR
          " resolved",
          resolved_or->size() - error_list.size(), resolved_or->size());
      error = GRPC_ERROR_CREATE_REFERENCING(msg.c_str(), error_list.data(),
                                            error_list.size());
      LOG(INFO) << "WARNING: " << StatusToString(error);
      // we managed to bind some addresses: continue without error
    }
    return absl::OkStatus();
  }();  // lambda end
  if (!error.ok()) *port_num = 0;
  return error;
}

}  // namespace

namespace experimental {

absl::Status PassiveListenerImpl::AcceptConnectedEndpoint(
    std::unique_ptr<EventEngine::Endpoint> endpoint) {
  CHECK_NE(server_.get(), nullptr);
  RefCountedPtr<Chttp2ServerListener> listener;
  {
    MutexLock lock(&mu_);
    if (listener_ != nullptr) {
      listener =
          listener_->RefIfNonZero().TakeAsSubclass<Chttp2ServerListener>();
    }
  }
  if (listener == nullptr) {
    return absl::UnavailableError("passive listener already shut down");
  }
  ExecCtx exec_ctx;
  listener->AcceptConnectedEndpoint(std::move(endpoint));
  return absl::OkStatus();
}

absl::Status PassiveListenerImpl::AcceptConnectedFd(int fd) {
  CHECK_NE(server_.get(), nullptr);
  ExecCtx exec_ctx;
  auto& args = server_->channel_args();
  auto* supports_fd = QueryExtension<EventEngineSupportsFdExtension>(
      /*engine=*/args.GetObjectRef<EventEngine>().get());
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
  listener_ = nullptr;
}

}  // namespace experimental
}  // namespace grpc_core

int grpc_server_add_http2_port(grpc_server* server, const char* addr,
                               grpc_server_credentials* creds) {
  grpc_core::ExecCtx exec_ctx;
  grpc_error_handle err;
  grpc_core::RefCountedPtr<grpc_server_security_connector> sc;
  int port_num = 0;
  grpc_core::Server* core_server = grpc_core::Server::FromC(server);
  grpc_core::ChannelArgs args = core_server->channel_args();
  GRPC_TRACE_LOG(api, INFO) << "grpc_server_add_http2_port(server=" << server
                            << ", addr=" << addr << ", creds=" << creds << ")";
  // Create security context.
  if (creds == nullptr) {
    err = GRPC_ERROR_CREATE(
        "No credentials specified for secure server port (creds==NULL)");
    goto done;
  }
  // TODO(yashykt): Ideally, we would not want to have different behavior here
  // based on whether a config fetcher is configured or not. Currently, we have
  // a feature for SSL credentials reloading with an application callback that
  // assumes that there is a single security connector. If we delay the creation
  // of the security connector to after the creation of the listener(s), we
  // would have potentially multiple security connectors which breaks the
  // assumption for SSL creds reloading. When the API for SSL creds reloading is
  // rewritten, we would be able to make this workaround go away by removing
  // that assumption. As an immediate drawback of this workaround, config
  // fetchers need to be registered before adding ports to the server.
  if (core_server->config_fetcher() != nullptr) {
    // Create channel args.
    args = args.SetObject(creds->Ref());
  } else {
    sc = creds->create_security_connector(grpc_core::ChannelArgs());
    if (sc == nullptr) {
      err = GRPC_ERROR_CREATE(absl::StrCat(
          "Unable to create secure server with credentials of type ",
          creds->type().name()));
      goto done;
    }
    args = args.SetObject(creds->Ref()).SetObject(sc);
  }
  // Add server port.
  err = grpc_core::Chttp2ServerAddPort(core_server, addr, args, &port_num);
done:
  sc.reset(DEBUG_LOCATION, "server");
  if (!err.ok()) {
    LOG(ERROR) << grpc_core::StatusToString(err);
  }
  return port_num;
}

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
      core_server->SetupTransport(transport, nullptr, server_args, nullptr);
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
      grpc_core::Chttp2ServerListener::CreateForPassiveListener(
          server, args, passive_listener);
  passive_listener->server_ = server->Ref();
  return absl::OkStatus();
}
