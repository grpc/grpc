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
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/insecure/insecure_credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
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

class Chttp2ServerListener : public Server::ListenerInterface {
 public:
  static grpc_error_handle Create(Server* server,
                                  const EventEngine::ResolvedAddress& addr,
                                  const ChannelArgs& args, int* port_num);

  static grpc_error_handle CreateWithAcceptor(Server* server, const char* name,
                                              const ChannelArgs& args);

  static Chttp2ServerListener* CreateForPassiveListener(
      Server* server, const ChannelArgs& args,
      std::shared_ptr<experimental::PassiveListenerImpl> passive_listener);

  // Do not instantiate directly.  Use one of the factory methods above.
  Chttp2ServerListener(Server* server, const ChannelArgs& args,
                       ServerConfigFetcher* config_fetcher,
                       std::shared_ptr<experimental::PassiveListenerImpl>
                           passive_listener = nullptr);
  ~Chttp2ServerListener() override;

  void Start() override;

  void AcceptConnectedEndpoint(std::unique_ptr<EventEngine::Endpoint> endpoint);

  channelz::ListenSocketNode* channelz_listen_socket_node() const override {
    return channelz_listen_socket_.get();
  }

  void SetServerListenerState(
      RefCountedPtr<Server::ListenerState> /*listener_state*/) override {}

  const grpc_resolved_address* resolved_address() const override {
    // Should only be invoked with experiment server_listener
    Crash("Illegal");
    return nullptr;
  }

  void SetOnDestroyDone(grpc_closure* on_destroy_done) override;

  void Orphan() override;

 private:
  friend class experimental::PassiveListenerImpl;

  class ConfigFetcherWatcher : public ServerConfigFetcher::WatcherInterface {
   public:
    explicit ConfigFetcherWatcher(RefCountedPtr<Chttp2ServerListener> listener)
        : listener_(std::move(listener)) {}

    void UpdateConnectionManager(
        RefCountedPtr<ServerConfigFetcher::ConnectionManager>
            connection_manager) override;

    void StopServing() override;

   private:
    RefCountedPtr<Chttp2ServerListener> listener_;
  };

  class ActiveConnection : public InternallyRefCounted<ActiveConnection> {
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
      std::optional<EventEngine::TaskHandle> timer_handle_
          ABSL_GUARDED_BY(&ActiveConnection::mu_);
      grpc_closure on_receive_settings_ ABSL_GUARDED_BY(&ActiveConnection::mu_);
      grpc_pollset_set* const interested_parties_;
    };

    ActiveConnection(RefCountedPtr<Chttp2ServerListener> listener,
                     grpc_pollset* accepting_pollset, AcceptorPtr acceptor,
                     EventEngine* event_engine, const ChannelArgs& args,
                     MemoryOwner memory_owner);

    void Orphan() override;

    void SendGoAway();

    void Start(OrphanablePtr<grpc_endpoint> endpoint, const ChannelArgs& args);

    // Needed to be able to grab an external ref in
    // Chttp2ServerListener::OnAccept()
    using InternallyRefCounted<ActiveConnection>::Ref;

   private:
    static void OnClose(void* arg, grpc_error_handle error);
    void OnDrainGraceTimeExpiry() ABSL_LOCKS_EXCLUDED(&mu_);

    RefCountedPtr<Chttp2ServerListener> listener_;
    Mutex mu_ ABSL_ACQUIRED_AFTER(&listener_->mu_);
    // Was ActiveConnection::Start() invoked? Used to determine whether
    // tcp_server needs to be unreffed.
    bool connection_started_ ABSL_GUARDED_BY(&mu_) = false;
    // Set by HandshakingState before the handshaking begins and reset when
    // handshaking is done.
    OrphanablePtr<HandshakingState> handshaking_state_ ABSL_GUARDED_BY(&mu_);
    // Set by HandshakingState when handshaking is done and a valid transport
    // is created.
    RefCountedPtr<grpc_chttp2_transport> transport_ ABSL_GUARDED_BY(&mu_) =
        nullptr;
    grpc_closure on_close_;
    std::optional<EventEngine::TaskHandle> drain_grace_timer_handle_
        ABSL_GUARDED_BY(&mu_);
    // Use a raw pointer since this event_engine_ is grabbed from the
    // ChannelArgs of the listener_.
    EventEngine* const event_engine_ ABSL_GUARDED_BY(&mu_);
    bool shutdown_ ABSL_GUARDED_BY(&mu_) = false;
  };

  // To allow access to RefCounted<> like interface.
  friend class RefCountedPtr<Chttp2ServerListener>;

  // Should only be called once so as to start the TCP server.
  void StartListening();

  static void OnAccept(void* arg, grpc_endpoint* tcp,
                       grpc_pollset* accepting_pollset,
                       grpc_tcp_server_acceptor* acceptor);

  static void TcpServerShutdownComplete(void* arg, grpc_error_handle error);

  static void DestroyListener(Server* /*server*/, void* arg,
                              grpc_closure* destroy_done);

  Server* const server_ = nullptr;
  grpc_tcp_server* tcp_server_ = nullptr;
  grpc_resolved_address resolved_address_;
  ConfigFetcherWatcher* config_fetcher_watcher_ = nullptr;
  ChannelArgs args_;
  Mutex mu_;
  RefCountedPtr<ServerConfigFetcher::ConnectionManager> connection_manager_
      ABSL_GUARDED_BY(mu_);
  // Signals whether grpc_tcp_server_start() has been called.
  bool started_ ABSL_GUARDED_BY(mu_) = false;
  // Signals whether grpc_tcp_server_start() has completed.
  CondVar started_cv_ ABSL_GUARDED_BY(mu_);
  // Signals whether new requests/connections are to be accepted.
  bool is_serving_ ABSL_GUARDED_BY(mu_) = false;
  // Signals whether the application has triggered shutdown.
  bool shutdown_ ABSL_GUARDED_BY(mu_) = false;
  std::map<ActiveConnection*, OrphanablePtr<ActiveConnection>> connections_
      ABSL_GUARDED_BY(mu_);
  grpc_closure tcp_server_shutdown_complete_ ABSL_GUARDED_BY(mu_);
  grpc_closure* on_destroy_done_ ABSL_GUARDED_BY(mu_) = nullptr;
  RefCountedPtr<channelz::ListenSocketNode> channelz_listen_socket_;
  MemoryQuotaRefPtr memory_quota_;
  ConnectionQuotaRefPtr connection_quota_;
  ServerConfigFetcher* config_fetcher_ = nullptr;
  // TODO(yashykt): consider using std::variant<> to minimize memory usage for
  // disjoint cases where different fields are used.
  std::shared_ptr<experimental::PassiveListenerImpl> passive_listener_;
};

//
// Chttp2ServerListener::ConfigFetcherWatcher
//

void Chttp2ServerListener::ConfigFetcherWatcher::UpdateConnectionManager(
    RefCountedPtr<ServerConfigFetcher::ConnectionManager> connection_manager) {
  RefCountedPtr<ServerConfigFetcher::ConnectionManager>
      connection_manager_to_destroy;
  class GracefulShutdownExistingConnections {
   public:
    ~GracefulShutdownExistingConnections() {
      // Send GOAWAYs on the transports so that they get disconnected when
      // existing RPCs finish, and so that no new RPC is started on them.
      for (auto& connection : connections_) {
        connection.first->SendGoAway();
      }
    }

    void set_connections(
        std::map<ActiveConnection*, OrphanablePtr<ActiveConnection>>
            connections) {
      CHECK(connections_.empty());
      connections_ = std::move(connections);
    }

   private:
    std::map<ActiveConnection*, OrphanablePtr<ActiveConnection>> connections_;
  } connections_to_shutdown;
  {
    MutexLock lock(&listener_->mu_);
    connection_manager_to_destroy = listener_->connection_manager_;
    listener_->connection_manager_ = std::move(connection_manager);
    connections_to_shutdown.set_connections(std::move(listener_->connections_));
    if (listener_->shutdown_) {
      return;
    }
    listener_->is_serving_ = true;
    if (listener_->started_) return;
  }
  int port_temp;
  grpc_error_handle error = grpc_tcp_server_add_port(
      listener_->tcp_server_, &listener_->resolved_address_, &port_temp);
  if (!error.ok()) {
    LOG(ERROR) << "Error adding port to server: " << StatusToString(error);
    // TODO(yashykt): We wouldn't need to assert here if we bound to the
    // port earlier during AddPort.
    CHECK(0);
  }
  listener_->StartListening();
  {
    MutexLock lock(&listener_->mu_);
    listener_->started_ = true;
    listener_->started_cv_.SignalAll();
  }
}

void Chttp2ServerListener::ConfigFetcherWatcher::StopServing() {
  std::map<ActiveConnection*, OrphanablePtr<ActiveConnection>> connections;
  {
    MutexLock lock(&listener_->mu_);
    listener_->is_serving_ = false;
    connections = std::move(listener_->connections_);
  }
  // Send GOAWAYs on the transports so that they disconnected when existing
  // RPCs finish.
  for (auto& connection : connections) {
    connection.first->SendGoAway();
  }
}

//
// Chttp2ServerListener::ActiveConnection::HandshakingState
//

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
  bool connection_started = false;
  {
    MutexLock lock(&connection_->mu_);
    connection_started = connection_->connection_started_;
  }
  if (accepting_pollset_ != nullptr) {
    grpc_pollset_set_del_pollset(interested_parties_, accepting_pollset_);
  }
  grpc_pollset_set_destroy(interested_parties_);
  if (connection_started && connection_->listener_ != nullptr &&
      connection_->listener_->tcp_server_ != nullptr) {
    grpc_tcp_server_unref(connection_->listener_->tcp_server_);
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
      self->connection_->event_engine_->Cancel(*self->timer_handle_);
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
            connection_->listener_->server_->SetupTransport(
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
          // need to watch on the transport being closed so that we can an
          // updated list of active connections.
          grpc_closure* on_close = nullptr;
          if (connection_->listener_->config_fetcher_watcher_ != nullptr) {
            // Refs helds by OnClose()
            connection_->Ref().release();
            on_close = &connection_->on_close_;
          } else {
            // Remove the connection from the connections_ map since OnClose()
            // will not be invoked when a config fetcher is set.
            auto connection_quota =
                connection_->listener_->connection_quota_->Ref().release();
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
          timer_handle_ = connection_->event_engine_->RunAfter(
              deadline_ - Timestamp::Now(), [self = Ref()]() mutable {
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
  OrphanablePtr<ActiveConnection> connection;
  if (cleanup_connection) {
    MutexLock listener_lock(&connection_->listener_->mu_);
    if (release_connection) {
      connection_->listener_->connection_quota_->ReleaseConnections(1);
    }
    auto it = connection_->listener_->connections_.find(connection_.get());
    if (it != connection_->listener_->connections_.end()) {
      connection = std::move(it->second);
      connection_->listener_->connections_.erase(it);
    }
  }
}

//
// Chttp2ServerListener::ActiveConnection
//

Chttp2ServerListener::ActiveConnection::ActiveConnection(
    RefCountedPtr<Chttp2ServerListener> listener,
    grpc_pollset* accepting_pollset, AcceptorPtr acceptor,
    EventEngine* event_engine, const ChannelArgs& args,
    MemoryOwner memory_owner)
    : listener_(std::move(listener)),
      handshaking_state_(memory_owner.MakeOrphanable<HandshakingState>(
          Ref(), accepting_pollset, std::move(acceptor), args)),
      event_engine_(event_engine) {
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

void Chttp2ServerListener::ActiveConnection::SendGoAway() {
  grpc_chttp2_transport* transport = nullptr;
  {
    MutexLock lock(&mu_);
    if (!shutdown_) {
      // Send a GOAWAY if the transport exists
      if (transport_ != nullptr) {
        transport = transport_.get();
        drain_grace_timer_handle_ = event_engine_->RunAfter(
            std::max(Duration::Zero(),
                     listener_->args_
                         .GetDurationFromIntMillis(
                             GRPC_ARG_SERVER_CONFIG_CHANGE_DRAIN_GRACE_TIME_MS)
                         .value_or(Duration::Minutes(10))),
            [self = Ref(DEBUG_LOCATION, "drain_grace_timer")]() mutable {
              ExecCtx exec_ctx;
              self->OnDrainGraceTimeExpiry();
              self.reset(DEBUG_LOCATION, "drain_grace_timer");
            });
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
    // Set an HTTP2 error of NO_ERROR to do graceful GOAWAYs.
    op->goaway_error = grpc_error_set_int(
        GRPC_ERROR_CREATE("Server is stopping to serve requests."),
        StatusIntProperty::kHttp2Error, GRPC_HTTP2_NO_ERROR);
    transport->PerformOp(op);
  }
}

void Chttp2ServerListener::ActiveConnection::Start(
    OrphanablePtr<grpc_endpoint> endpoint, const ChannelArgs& args) {
  RefCountedPtr<HandshakingState> handshaking_state_ref;
  {
    MutexLock lock(&mu_);
    connection_started_ = true;
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
  OrphanablePtr<ActiveConnection> connection;
  {
    MutexLock listener_lock(&self->listener_->mu_);
    MutexLock connection_lock(&self->mu_);
    // The node was already deleted from the connections_ list if the
    // connection is shutdown.
    if (!self->shutdown_) {
      auto it = self->listener_->connections_.find(self);
      if (it != self->listener_->connections_.end()) {
        connection = std::move(it->second);
        self->listener_->connections_.erase(it);
      }
      self->shutdown_ = true;
    }
    // Cancel the drain_grace_timer_ if needed.
    if (self->drain_grace_timer_handle_.has_value()) {
      self->event_engine_->Cancel(*self->drain_grace_timer_handle_);
      self->drain_grace_timer_handle_.reset();
    }
  }
  self->listener_->connection_quota_->ReleaseConnections(1);
  self->Unref();
}

void Chttp2ServerListener::ActiveConnection::OnDrainGraceTimeExpiry() {
  grpc_chttp2_transport* transport = nullptr;
  // If the drain_grace_timer_ was not cancelled, disconnect the transport
  // immediately.
  {
    MutexLock lock(&mu_);
    if (drain_grace_timer_handle_.has_value()) {
      transport = transport_.get();
      drain_grace_timer_handle_.reset();
    }
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

grpc_error_handle Chttp2ServerListener::Create(
    Server* server, const EventEngine::ResolvedAddress& addr,
    const ChannelArgs& args, int* port_num) {
  // Create Chttp2ServerListener.
  OrphanablePtr<Chttp2ServerListener> listener =
      MakeOrphanable<Chttp2ServerListener>(server, args,
                                           server->config_fetcher());
  // The tcp_server will be unreffed when the listener is orphaned, which could
  // be at the end of this function if the listener was not added to the
  // server's set of listeners.
  grpc_error_handle error = grpc_tcp_server_create(
      &listener->tcp_server_shutdown_complete_, ChannelArgsEndpointConfig(args),
      OnAccept, listener.get(), &listener->tcp_server_);
  if (!error.ok()) return error;
  // TODO(yijiem): remove this conversion when we remove all
  // grpc_resolved_address usages.
  grpc_resolved_address iomgr_addr =
      grpc_event_engine::experimental::CreateGRPCResolvedAddress(addr);
  if (listener->config_fetcher_ != nullptr) {
    listener->resolved_address_ = iomgr_addr;
    // TODO(yashykt): Consider binding so as to be able to return the port
    // number.
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

grpc_error_handle Chttp2ServerListener::CreateWithAcceptor(
    Server* server, const char* name, const ChannelArgs& args) {
  auto listener = MakeOrphanable<Chttp2ServerListener>(
      server, args, server->config_fetcher());
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
      server, args, nullptr, std::move(passive_listener));
  auto listener_ptr = listener.get();
  server->AddListener(std::move(listener));
  return listener_ptr;
}

Chttp2ServerListener::Chttp2ServerListener(
    Server* server, const ChannelArgs& args,
    ServerConfigFetcher* config_fetcher,
    std::shared_ptr<experimental::PassiveListenerImpl> passive_listener)
    : server_(server),
      args_(args),
      memory_quota_(args.GetObject<ResourceQuota>()->memory_quota()),
      connection_quota_(MakeRefCounted<ConnectionQuota>()),
      config_fetcher_(config_fetcher),
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
  if (passive_listener_ != nullptr) {
    passive_listener_->ListenerDestroyed();
  }
  if (on_destroy_done_ != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, on_destroy_done_, absl::OkStatus());
  }
}

// Server callback: start listening on our ports
void Chttp2ServerListener::Start() {
  if (config_fetcher_ != nullptr) {
    auto watcher = std::make_unique<ConfigFetcherWatcher>(
        RefAsSubclass<Chttp2ServerListener>());
    config_fetcher_watcher_ = watcher.get();
    config_fetcher_->StartWatch(
        grpc_sockaddr_to_string(&resolved_address_, false).value(),
        std::move(watcher));
  } else {
    {
      MutexLock lock(&mu_);
      started_ = true;
      is_serving_ = true;
    }
    StartListening();
  }
}

void Chttp2ServerListener::StartListening() {
  if (tcp_server_ != nullptr) {
    grpc_tcp_server_start(tcp_server_, &server_->pollsets());
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

namespace {

ChannelArgs ModifyArgsForConnection(const ChannelArgs& args,
                                    grpc_error_handle* error) {
  auto* server_credentials = args.GetObject<grpc_server_credentials>();
  if (server_credentials == nullptr) {
    *error = GRPC_ERROR_CREATE("Could not find server credentials");
    return args;
  }
  auto security_connector = server_credentials->create_security_connector(args);
  if (security_connector == nullptr) {
    *error = GRPC_ERROR_CREATE(
        absl::StrCat("Unable to create secure server with credentials of type ",
                     server_credentials->type().name()));
    return args;
  }
  return args.SetObject(security_connector);
}

}  // namespace

void Chttp2ServerListener::OnAccept(void* arg, grpc_endpoint* tcp,
                                    grpc_pollset* accepting_pollset,
                                    grpc_tcp_server_acceptor* server_acceptor) {
  Chttp2ServerListener* self = static_cast<Chttp2ServerListener*>(arg);
  ChannelArgs args = self->args_;
  OrphanablePtr<grpc_endpoint> endpoint(tcp);
  AcceptorPtr acceptor(server_acceptor);
  RefCountedPtr<ServerConfigFetcher::ConnectionManager> connection_manager;
  {
    MutexLock lock(&self->mu_);
    connection_manager = self->connection_manager_;
  }
  if (!self->connection_quota_->AllowIncomingConnection(
          self->memory_quota_, grpc_endpoint_get_peer(endpoint.get()))) {
    return;
  }
  if (self->config_fetcher_ != nullptr) {
    if (connection_manager == nullptr) {
      return;
    }
    absl::StatusOr<ChannelArgs> args_result =
        connection_manager->UpdateChannelArgsForConnection(args, tcp);
    if (!args_result.ok()) {
      return;
    }
    grpc_error_handle error;
    args = ModifyArgsForConnection(*args_result, &error);
    if (!error.ok()) {
      return;
    }
  }
  auto memory_owner = self->memory_quota_->CreateMemoryOwner();
  EventEngine* const event_engine = self->args_.GetObject<EventEngine>();
  auto connection = memory_owner.MakeOrphanable<ActiveConnection>(
      self->RefAsSubclass<Chttp2ServerListener>(), accepting_pollset,
      std::move(acceptor), event_engine, args, std::move(memory_owner));
  // Hold a ref to connection to allow starting handshake outside the
  // critical region
  RefCountedPtr<ActiveConnection> connection_ref = connection->Ref();
  {
    MutexLock lock(&self->mu_);
    // Shutdown the the connection if listener's stopped serving or if the
    // connection manager has changed.
    if (!self->shutdown_ && self->is_serving_ &&
        connection_manager == self->connection_manager_) {
      // The ref for the tcp_server needs to be taken in the critical region
      // after having made sure that the listener has not been Orphaned, so as
      // to avoid heap-use-after-free issues where `Ref()` is invoked when the
      // listener is already shutdown. Note that the listener holds a ref to the
      // tcp_server but this ref is given away when the listener is orphaned
      // (shutdown). A connection needs the tcp_server to outlast the handshake
      // since the acceptor needs it.
      if (self->tcp_server_ != nullptr) {
        grpc_tcp_server_ref(self->tcp_server_);
      }
      self->connections_.emplace(connection.get(), std::move(connection));
    }
  }
  if (connection == nullptr) {
    connection_ref->Start(std::move(endpoint), args);
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
void Chttp2ServerListener::Orphan() {
  // Cancel the watch before shutting down so as to avoid holding a ref to the
  // listener in the watcher.
  if (config_fetcher_watcher_ != nullptr) {
    CHECK_NE(config_fetcher_, nullptr);
    config_fetcher_->CancelWatch(config_fetcher_watcher_);
  }
  std::map<ActiveConnection*, OrphanablePtr<ActiveConnection>> connections;
  grpc_tcp_server* tcp_server;
  {
    MutexLock lock(&mu_);
    shutdown_ = true;
    is_serving_ = false;
    // Orphan the connections so that they can start cleaning up.
    connections = std::move(connections_);
    // If the listener is currently set to be serving but has not been started
    // yet, it means that `grpc_tcp_server_start` is in progress. Wait for the
    // operation to finish to avoid causing races.
    while (is_serving_ && !started_) {
      started_cv_.Wait(&mu_);
    }
    tcp_server = tcp_server_;
  }
  if (tcp_server != nullptr) {
    grpc_tcp_server_shutdown_listeners(tcp_server);
    grpc_tcp_server_unref(tcp_server);
  } else {
    Unref();
  }
}

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
            transport.get(), accepting_pollset_, (*result)->args,
            grpc_chttp2_transport_get_socket_node(transport.get()));
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
                StatusIntProperty::kHttp2Error, GRPC_HTTP2_NO_ERROR);
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

namespace {

grpc_error_handle Chttp2ServerAddPort(Server* server, const char* addr,
                                      const ChannelArgs& args, int* port_num) {
  if (addr == nullptr) {
    return GRPC_ERROR_CREATE("Invalid address: addr cannot be a nullptr.");
  }
  if (strncmp(addr, "external:", 9) == 0) {
    if (IsServerListenerEnabled()) {
      return NewChttp2ServerListener::CreateWithAcceptor(server, addr, args);
    } else {
      return Chttp2ServerListener::CreateWithAcceptor(server, addr, args);
    }
  }
  *port_num = -1;
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
      if (*port_num != -1 &&
          grpc_event_engine::experimental::ResolvedAddressGetPort(addr) == 0) {
        grpc_event_engine::experimental::ResolvedAddressSetPort(addr,
                                                                *port_num);
      }
      int port_temp = -1;
      if (IsServerListenerEnabled()) {
        error = NewChttp2ServerListener::Create(server, addr, args, &port_temp);
      } else {
        error = Chttp2ServerListener::Create(server, addr, args, &port_temp);
      }
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
  if (!error.ok()) *port_num = 0;
  return error;
}

}  // namespace

namespace experimental {

absl::Status PassiveListenerImpl::AcceptConnectedEndpoint(
    std::unique_ptr<EventEngine::Endpoint> endpoint) {
  CHECK_NE(server_.get(), nullptr);
  if (IsServerListenerEnabled()) {
    RefCountedPtr<NewChttp2ServerListener> new_listener;
    {
      MutexLock lock(&mu_);
      auto* new_listener_ptr =
          std::get_if<NewChttp2ServerListener*>(&listener_);
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
  } else {
    RefCountedPtr<Chttp2ServerListener> listener;
    {
      MutexLock lock(&mu_);
      auto* listener_ptr = std::get_if<Chttp2ServerListener*>(&listener_);
      if (listener_ptr != nullptr && *listener_ptr != nullptr) {
        listener = (*listener_ptr)
                       ->RefIfNonZero()
                       .TakeAsSubclass<Chttp2ServerListener>();
      }
    }
    if (listener == nullptr) {
      return absl::UnavailableError("passive listener already shut down");
    }
    ExecCtx exec_ctx;
    listener->AcceptConnectedEndpoint(std::move(endpoint));
  }
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
  listener_ = static_cast<Chttp2ServerListener*>(nullptr);
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
  if (grpc_core::IsServerListenerEnabled()) {
    passive_listener->listener_ =
        grpc_core::NewChttp2ServerListener::CreateForPassiveListener(
            server, args, passive_listener);
  } else {
    passive_listener->listener_ =
        grpc_core::Chttp2ServerListener::CreateForPassiveListener(
            server, args, passive_listener);
  }

  passive_listener->server_ = server->Ref();
  return absl::OkStatus();
}
