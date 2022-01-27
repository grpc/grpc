/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/server/chttp2_server.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/strip.h"

#include <grpc/grpc.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {
namespace {

const char kUnixUriPrefix[] = "unix:";
const char kUnixAbstractUriPrefix[] = "unix-abstract:";

class Chttp2ServerListener : public Server::ListenerInterface {
 public:
  static grpc_error_handle Create(Server* server, grpc_resolved_address* addr,
                                  grpc_channel_args* args,
                                  Chttp2ServerArgsModifier args_modifier,
                                  int* port_num);

  static grpc_error_handle CreateWithAcceptor(
      Server* server, const char* name, grpc_channel_args* args,
      Chttp2ServerArgsModifier args_modifier);

  // Do not instantiate directly.  Use one of the factory methods above.
  Chttp2ServerListener(Server* server, grpc_channel_args* args,
                       Chttp2ServerArgsModifier args_modifier);
  ~Chttp2ServerListener() override;

  void Start(Server* server,
             const std::vector<grpc_pollset*>* pollsets) override;

  channelz::ListenSocketNode* channelz_listen_socket_node() const override {
    return channelz_listen_socket_.get();
  }

  void SetOnDestroyDone(grpc_closure* on_destroy_done) override;

  void Orphan() override;

 private:
  class ConfigFetcherWatcher
      : public grpc_server_config_fetcher::WatcherInterface {
   public:
    explicit ConfigFetcherWatcher(RefCountedPtr<Chttp2ServerListener> listener)
        : listener_(std::move(listener)) {}

    void UpdateConnectionManager(
        RefCountedPtr<grpc_server_config_fetcher::ConnectionManager>
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
                       grpc_pollset* accepting_pollset,
                       grpc_tcp_server_acceptor* acceptor,
                       grpc_channel_args* args);

      ~HandshakingState() override;

      void Orphan() override;

      void Start(grpc_endpoint* endpoint, grpc_channel_args* args);

      // Needed to be able to grab an external ref in ActiveConnection::Start()
      using InternallyRefCounted<HandshakingState>::Ref;

     private:
      static void OnTimeout(void* arg, grpc_error_handle error);
      static void OnReceiveSettings(void* arg, grpc_error_handle /* error */);
      static void OnHandshakeDone(void* arg, grpc_error_handle error);
      RefCountedPtr<ActiveConnection> const connection_;
      grpc_pollset* const accepting_pollset_;
      grpc_tcp_server_acceptor* acceptor_;
      RefCountedPtr<HandshakeManager> handshake_mgr_
          ABSL_GUARDED_BY(&connection_->mu_);
      // State for enforcing handshake timeout on receiving HTTP/2 settings.
      grpc_millis const deadline_;
      grpc_timer timer_ ABSL_GUARDED_BY(&connection_->mu_);
      grpc_closure on_timeout_ ABSL_GUARDED_BY(&connection_->mu_);
      grpc_closure on_receive_settings_ ABSL_GUARDED_BY(&connection_->mu_);
      grpc_pollset_set* const interested_parties_;
    };

    ActiveConnection(grpc_pollset* accepting_pollset,
                     grpc_tcp_server_acceptor* acceptor,
                     grpc_channel_args* args, MemoryOwner memory_owner);
    ~ActiveConnection() override;

    void Orphan() override;

    void SendGoAway();

    void Start(RefCountedPtr<Chttp2ServerListener> listener,
               grpc_endpoint* endpoint, grpc_channel_args* args);

    // Needed to be able to grab an external ref in
    // Chttp2ServerListener::OnAccept()
    using InternallyRefCounted<ActiveConnection>::Ref;

   private:
    static void OnClose(void* arg, grpc_error_handle error);
    static void OnDrainGraceTimeExpiry(void* arg, grpc_error_handle error);

    RefCountedPtr<Chttp2ServerListener> listener_;
    Mutex mu_ ABSL_ACQUIRED_AFTER(&listener_->mu_);
    // Set by HandshakingState before the handshaking begins and reset when
    // handshaking is done.
    OrphanablePtr<HandshakingState> handshaking_state_ ABSL_GUARDED_BY(&mu_);
    // Set by HandshakingState when handshaking is done and a valid transport is
    // created.
    grpc_chttp2_transport* transport_ ABSL_GUARDED_BY(&mu_) = nullptr;
    grpc_closure on_close_;
    grpc_timer drain_grace_timer_;
    grpc_closure on_drain_grace_time_expiry_;
    bool drain_grace_timer_expiry_callback_pending_ ABSL_GUARDED_BY(&mu_) =
        false;
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

  // The interface required by RefCountedPtr<> has been manually implemented
  // here to take a ref on tcp_server_ instead. Note that, the handshaker needs
  // tcp_server_ to exist for the lifetime of the handshake since it's needed by
  // acceptor. Sharing refs between the listener and tcp_server_ is just an
  // optimization to avoid taking additional refs on the listener, since
  // TcpServerShutdownComplete already holds a ref to the listener.
  void IncrementRefCount() { grpc_tcp_server_ref(tcp_server_); }
  void IncrementRefCount(const DebugLocation& /* location */,
                         const char* /* reason */) {
    IncrementRefCount();
  }

  RefCountedPtr<Chttp2ServerListener> Ref() GRPC_MUST_USE_RESULT {
    IncrementRefCount();
    return RefCountedPtr<Chttp2ServerListener>(this);
  }
  RefCountedPtr<Chttp2ServerListener> Ref(const DebugLocation& /* location */,
                                          const char* /* reason */)
      GRPC_MUST_USE_RESULT {
    return Ref();
  }

  void Unref() { grpc_tcp_server_unref(tcp_server_); }
  void Unref(const DebugLocation& /* location */, const char* /* reason */) {
    Unref();
  }

  Server* const server_;
  grpc_tcp_server* tcp_server_;
  grpc_resolved_address resolved_address_;
  Chttp2ServerArgsModifier const args_modifier_;
  ConfigFetcherWatcher* config_fetcher_watcher_ = nullptr;
  grpc_channel_args* args_;
  Mutex mu_;
  RefCountedPtr<grpc_server_config_fetcher::ConnectionManager>
      connection_manager_ ABSL_GUARDED_BY(mu_);
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
};

//
// Chttp2ServerListener::ConfigFetcherWatcher
//

void Chttp2ServerListener::ConfigFetcherWatcher::UpdateConnectionManager(
    RefCountedPtr<grpc_server_config_fetcher::ConnectionManager>
        connection_manager) {
  RefCountedPtr<grpc_server_config_fetcher::ConnectionManager>
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
      GPR_ASSERT(connections_.empty());
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
  if (error != GRPC_ERROR_NONE) {
    GRPC_ERROR_UNREF(error);
    gpr_log(GPR_ERROR, "Error adding port to server: %s",
            grpc_error_std_string(error).c_str());
    // TODO(yashykt): We wouldn't need to assert here if we bound to the
    // port earlier during AddPort.
    GPR_ASSERT(0);
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
  // Send GOAWAYs on the transports so that they disconnected when existing RPCs
  // finish.
  for (auto& connection : connections) {
    connection.first->SendGoAway();
  }
}

//
// Chttp2ServerListener::ActiveConnection::HandshakingState
//

grpc_millis GetConnectionDeadline(const grpc_channel_args* args) {
  int timeout_ms =
      grpc_channel_args_find_integer(args, GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS,
                                     {120 * GPR_MS_PER_SEC, 1, INT_MAX});
  return ExecCtx::Get()->Now() + timeout_ms;
}

Chttp2ServerListener::ActiveConnection::HandshakingState::HandshakingState(
    RefCountedPtr<ActiveConnection> connection_ref,
    grpc_pollset* accepting_pollset, grpc_tcp_server_acceptor* acceptor,
    grpc_channel_args* args)
    : connection_(std::move(connection_ref)),
      accepting_pollset_(accepting_pollset),
      acceptor_(acceptor),
      handshake_mgr_(MakeRefCounted<HandshakeManager>()),
      deadline_(GetConnectionDeadline(args)),
      interested_parties_(grpc_pollset_set_create()) {
  grpc_pollset_set_add_pollset(interested_parties_, accepting_pollset_);
  CoreConfiguration::Get().handshaker_registry().AddHandshakers(
      HANDSHAKER_SERVER, args, interested_parties_, handshake_mgr_.get());
}

Chttp2ServerListener::ActiveConnection::HandshakingState::~HandshakingState() {
  grpc_pollset_set_del_pollset(interested_parties_, accepting_pollset_);
  grpc_pollset_set_destroy(interested_parties_);
  gpr_free(acceptor_);
}

void Chttp2ServerListener::ActiveConnection::HandshakingState::Orphan() {
  {
    MutexLock lock(&connection_->mu_);
    if (handshake_mgr_ != nullptr) {
      handshake_mgr_->Shutdown(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Listener stopped serving."));
    }
  }
  Unref();
}

void Chttp2ServerListener::ActiveConnection::HandshakingState::Start(
    grpc_endpoint* endpoint, grpc_channel_args* args) {
  Ref().release();  // Held by OnHandshakeDone
  RefCountedPtr<HandshakeManager> handshake_mgr;
  {
    MutexLock lock(&connection_->mu_);
    if (handshake_mgr_ == nullptr) return;
    handshake_mgr = handshake_mgr_;
  }
  handshake_mgr->DoHandshake(endpoint, args, deadline_, acceptor_,
                             OnHandshakeDone, this);
}

void Chttp2ServerListener::ActiveConnection::HandshakingState::OnTimeout(
    void* arg, grpc_error_handle error) {
  HandshakingState* self = static_cast<HandshakingState*>(arg);
  // Note that we may be called with GRPC_ERROR_NONE when the timer fires
  // or with an error indicating that the timer system is being shut down.
  if (error != GRPC_ERROR_CANCELLED) {
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->disconnect_with_error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Did not receive HTTP/2 settings before handshake timeout");
    grpc_chttp2_transport* transport = nullptr;
    {
      MutexLock lock(&self->connection_->mu_);
      transport = self->connection_->transport_;
    }
    grpc_transport_perform_op(&transport->base, op);
  }
  self->Unref();
}

void Chttp2ServerListener::ActiveConnection::HandshakingState::
    OnReceiveSettings(void* arg, grpc_error_handle /* error */) {
  HandshakingState* self = static_cast<HandshakingState*>(arg);
  grpc_timer_cancel(&self->timer_);
  self->Unref();
}

void Chttp2ServerListener::ActiveConnection::HandshakingState::OnHandshakeDone(
    void* arg, grpc_error_handle error) {
  auto* args = static_cast<HandshakerArgs*>(arg);
  HandshakingState* self = static_cast<HandshakingState*>(args->user_data);
  OrphanablePtr<HandshakingState> handshaking_state_ref;
  RefCountedPtr<HandshakeManager> handshake_mgr;
  bool cleanup_connection = false;
  {
    MutexLock connection_lock(&self->connection_->mu_);
    if (error != GRPC_ERROR_NONE || self->connection_->shutdown_) {
      std::string error_str = grpc_error_std_string(error);
      gpr_log(GPR_DEBUG, "Handshaking failed: %s", error_str.c_str());
      cleanup_connection = true;
      if (error == GRPC_ERROR_NONE && args->endpoint != nullptr) {
        // We were shut down or stopped serving after handshaking completed
        // successfully, so destroy the endpoint here.
        // TODO(ctiller): It is currently necessary to shutdown endpoints
        // before destroying them, even if we know that there are no
        // pending read/write callbacks.  This should be fixed, at which
        // point this can be removed.
        grpc_endpoint_shutdown(args->endpoint, GRPC_ERROR_NONE);
        grpc_endpoint_destroy(args->endpoint);
        grpc_channel_args_destroy(args->args);
        grpc_slice_buffer_destroy_internal(args->read_buffer);
        gpr_free(args->read_buffer);
      }
    } else {
      // If the handshaking succeeded but there is no endpoint, then the
      // handshaker may have handed off the connection to some external
      // code, so we can just clean up here without creating a transport.
      if (args->endpoint != nullptr) {
        grpc_transport* transport =
            grpc_create_chttp2_transport(args->args, args->endpoint, false);
        grpc_error_handle channel_init_err =
            self->connection_->listener_->server_->SetupTransport(
                transport, self->accepting_pollset_, args->args,
                grpc_chttp2_transport_get_socket_node(transport));
        if (channel_init_err == GRPC_ERROR_NONE) {
          // Use notify_on_receive_settings callback to enforce the
          // handshake deadline.
          // Note: The reinterpret_cast<>s here are safe, because
          // grpc_chttp2_transport is a C-style extension of
          // grpc_transport, so this is morally equivalent of a
          // static_cast<> to a derived class.
          // TODO(roth): Change to static_cast<> when we C++-ify the
          // transport API.
          self->connection_->transport_ =
              reinterpret_cast<grpc_chttp2_transport*>(transport);
          GRPC_CHTTP2_REF_TRANSPORT(self->connection_->transport_,
                                    "ActiveConnection");  // Held by connection_
          self->Ref().release();  // Held by OnReceiveSettings().
          GRPC_CLOSURE_INIT(&self->on_receive_settings_, OnReceiveSettings,
                            self, grpc_schedule_on_exec_ctx);
          // If the listener has been configured with a config fetcher, we need
          // to watch on the transport being closed so that we can an updated
          // list of active connections.
          grpc_closure* on_close = nullptr;
          if (self->connection_->listener_->config_fetcher_watcher_ !=
              nullptr) {
            // Refs helds by OnClose()
            self->connection_->Ref().release();
            on_close = &self->connection_->on_close_;
          } else {
            // Remove the connection from the connections_ map since OnClose()
            // will not be invoked when a config fetcher is set.
            cleanup_connection = true;
          }
          grpc_chttp2_transport_start_reading(transport, args->read_buffer,
                                              &self->on_receive_settings_,
                                              on_close);
          grpc_channel_args_destroy(args->args);
          self->Ref().release();  // Held by OnTimeout().
          GRPC_CLOSURE_INIT(&self->on_timeout_, OnTimeout, self,
                            grpc_schedule_on_exec_ctx);
          grpc_timer_init(&self->timer_, self->deadline_, &self->on_timeout_);
        } else {
          // Failed to create channel from transport. Clean up.
          gpr_log(GPR_ERROR, "Failed to create channel: %s",
                  grpc_error_std_string(channel_init_err).c_str());
          GRPC_ERROR_UNREF(channel_init_err);
          grpc_transport_destroy(transport);
          grpc_slice_buffer_destroy_internal(args->read_buffer);
          gpr_free(args->read_buffer);
          cleanup_connection = true;
          grpc_channel_args_destroy(args->args);
        }
      } else {
        cleanup_connection = true;
      }
    }
    // Since the handshake manager is done, the connection no longer needs to
    // shutdown the handshake when the listener needs to stop serving.
    // Avoid calling the destructor of HandshakeManager and HandshakingState
    // from within the critical region.
    handshake_mgr = std::move(self->handshake_mgr_);
    handshaking_state_ref = std::move(self->connection_->handshaking_state_);
  }
  gpr_free(self->acceptor_);
  self->acceptor_ = nullptr;
  OrphanablePtr<ActiveConnection> connection;
  if (cleanup_connection) {
    MutexLock listener_lock(&self->connection_->listener_->mu_);
    auto it = self->connection_->listener_->connections_.find(
        self->connection_.get());
    if (it != self->connection_->listener_->connections_.end()) {
      connection = std::move(it->second);
      self->connection_->listener_->connections_.erase(it);
    }
  }
  self->Unref();
}

//
// Chttp2ServerListener::ActiveConnection
//

Chttp2ServerListener::ActiveConnection::ActiveConnection(
    grpc_pollset* accepting_pollset, grpc_tcp_server_acceptor* acceptor,
    grpc_channel_args* args, MemoryOwner memory_owner)
    : handshaking_state_(memory_owner.MakeOrphanable<HandshakingState>(
          Ref(), accepting_pollset, acceptor, args)) {
  GRPC_CLOSURE_INIT(&on_close_, ActiveConnection::OnClose, this,
                    grpc_schedule_on_exec_ctx);
}

Chttp2ServerListener::ActiveConnection::~ActiveConnection() {
  if (transport_ != nullptr) {
    GRPC_CHTTP2_UNREF_TRANSPORT(transport_, "ActiveConnection");
  }
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
    if (transport_ != nullptr && !shutdown_) {
      transport = transport_;
      Ref().release();  // Ref held by OnDrainGraceTimeExpiry
      GRPC_CLOSURE_INIT(&on_drain_grace_time_expiry_, OnDrainGraceTimeExpiry,
                        this, nullptr);
      grpc_timer_init(&drain_grace_timer_,
                      ExecCtx::Get()->Now() +
                          grpc_channel_args_find_integer(
                              listener_->args_,
                              GRPC_ARG_SERVER_CONFIG_CHANGE_DRAIN_GRACE_TIME_MS,
                              {10 * 60 * GPR_MS_PER_SEC, 0, INT_MAX}),
                      &on_drain_grace_time_expiry_);
      drain_grace_timer_expiry_callback_pending_ = true;
      shutdown_ = true;
    }
  }
  if (transport != nullptr) {
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->goaway_error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Server is stopping to serve requests.");
    grpc_transport_perform_op(&transport->base, op);
  }
}

void Chttp2ServerListener::ActiveConnection::Start(
    RefCountedPtr<Chttp2ServerListener> listener, grpc_endpoint* endpoint,
    grpc_channel_args* args) {
  RefCountedPtr<HandshakingState> handshaking_state_ref;
  listener_ = std::move(listener);
  {
    MutexLock lock(&mu_);
    if (shutdown_) return;
    // Hold a ref to HandshakingState to allow starting the handshake outside
    // the critical region.
    handshaking_state_ref = handshaking_state_->Ref();
  }
  handshaking_state_ref->Start(endpoint, args);
}

void Chttp2ServerListener::ActiveConnection::OnClose(
    void* arg, grpc_error_handle /* error */) {
  ActiveConnection* self = static_cast<ActiveConnection*>(arg);
  OrphanablePtr<ActiveConnection> connection;
  {
    MutexLock listener_lock(&self->listener_->mu_);
    MutexLock connection_lock(&self->mu_);
    // The node was already deleted from the connections_ list if the connection
    // is shutdown.
    if (!self->shutdown_) {
      auto it = self->listener_->connections_.find(self);
      if (it != self->listener_->connections_.end()) {
        connection = std::move(it->second);
        self->listener_->connections_.erase(it);
      }
      self->shutdown_ = true;
    }
    // Cancel the drain_grace_timer_ if needed.
    if (self->drain_grace_timer_expiry_callback_pending_) {
      grpc_timer_cancel(&self->drain_grace_timer_);
    }
  }
  self->Unref();
}

void Chttp2ServerListener::ActiveConnection::OnDrainGraceTimeExpiry(
    void* arg, grpc_error_handle error) {
  ActiveConnection* self = static_cast<ActiveConnection*>(arg);
  // If the drain_grace_timer_ was not cancelled, disconnect the transport
  // immediately.
  if (error == GRPC_ERROR_NONE) {
    grpc_chttp2_transport* transport = nullptr;
    {
      MutexLock lock(&self->mu_);
      transport = self->transport_;
    }
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->disconnect_with_error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Drain grace time expired. Closing connection immediately.");
    grpc_transport_perform_op(&transport->base, op);
  }
  self->Unref();
}

//
// Chttp2ServerListener
//

grpc_error_handle Chttp2ServerListener::Create(
    Server* server, grpc_resolved_address* addr, grpc_channel_args* args,
    Chttp2ServerArgsModifier args_modifier, int* port_num) {
  Chttp2ServerListener* listener = nullptr;
  // The bulk of this method is inside of a lambda to make cleanup
  // easier without using goto.
  grpc_error_handle error = [&]() {
    grpc_error_handle error = GRPC_ERROR_NONE;
    // Create Chttp2ServerListener.
    listener = new Chttp2ServerListener(server, args, args_modifier);
    error = grpc_tcp_server_create(&listener->tcp_server_shutdown_complete_,
                                   args, &listener->tcp_server_);
    if (error != GRPC_ERROR_NONE) return error;
    if (server->config_fetcher() != nullptr) {
      listener->resolved_address_ = *addr;
      // TODO(yashykt): Consider binding so as to be able to return the port
      // number.
    } else {
      error = grpc_tcp_server_add_port(listener->tcp_server_, addr, port_num);
      if (error != GRPC_ERROR_NONE) return error;
    }
    // Create channelz node.
    if (grpc_channel_args_find_bool(args, GRPC_ARG_ENABLE_CHANNELZ,
                                    GRPC_ENABLE_CHANNELZ_DEFAULT)) {
      std::string string_address = grpc_sockaddr_to_uri(addr);
      listener->channelz_listen_socket_ =
          MakeRefCounted<channelz::ListenSocketNode>(
              string_address.c_str(),
              absl::StrFormat("chttp2 listener %s", string_address.c_str()));
    }
    // Register with the server only upon success
    server->AddListener(OrphanablePtr<Server::ListenerInterface>(listener));
    return GRPC_ERROR_NONE;
  }();
  if (error != GRPC_ERROR_NONE) {
    if (listener != nullptr) {
      if (listener->tcp_server_ != nullptr) {
        // listener is deleted when tcp_server_ is shutdown.
        grpc_tcp_server_unref(listener->tcp_server_);
      } else {
        delete listener;
      }
    } else {
      grpc_channel_args_destroy(args);
    }
  }
  return error;
}

grpc_error_handle Chttp2ServerListener::CreateWithAcceptor(
    Server* server, const char* name, grpc_channel_args* args,
    Chttp2ServerArgsModifier args_modifier) {
  Chttp2ServerListener* listener =
      new Chttp2ServerListener(server, args, args_modifier);
  grpc_error_handle error = grpc_tcp_server_create(
      &listener->tcp_server_shutdown_complete_, args, &listener->tcp_server_);
  if (error != GRPC_ERROR_NONE) {
    delete listener;
    return error;
  }
  // TODO(yangg) channelz
  TcpServerFdHandler** arg_val =
      grpc_channel_args_find_pointer<TcpServerFdHandler*>(args, name);
  *arg_val = grpc_tcp_server_create_fd_handler(listener->tcp_server_);
  server->AddListener(OrphanablePtr<Server::ListenerInterface>(listener));
  return GRPC_ERROR_NONE;
}

Chttp2ServerListener::Chttp2ServerListener(
    Server* server, grpc_channel_args* args,
    Chttp2ServerArgsModifier args_modifier)
    : server_(server),
      args_modifier_(args_modifier),
      args_(args),
      memory_quota_(ResourceQuotaFromChannelArgs(args)->memory_quota()) {
  GRPC_CLOSURE_INIT(&tcp_server_shutdown_complete_, TcpServerShutdownComplete,
                    this, grpc_schedule_on_exec_ctx);
}

Chttp2ServerListener::~Chttp2ServerListener() {
  // Flush queued work before destroying handshaker factory, since that
  // may do a synchronous unref.
  ExecCtx::Get()->Flush();
  if (on_destroy_done_ != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, on_destroy_done_, GRPC_ERROR_NONE);
    ExecCtx::Get()->Flush();
  }
  grpc_channel_args_destroy(args_);
}

/* Server callback: start listening on our ports */
void Chttp2ServerListener::Start(
    Server* /*server*/, const std::vector<grpc_pollset*>* /* pollsets */) {
  if (server_->config_fetcher() != nullptr) {
    auto watcher = absl::make_unique<ConfigFetcherWatcher>(Ref());
    config_fetcher_watcher_ = watcher.get();
    server_->config_fetcher()->StartWatch(
        grpc_sockaddr_to_string(&resolved_address_, false), std::move(watcher));
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
  grpc_tcp_server_start(tcp_server_, &server_->pollsets(), OnAccept, this);
}

void Chttp2ServerListener::SetOnDestroyDone(grpc_closure* on_destroy_done) {
  MutexLock lock(&mu_);
  on_destroy_done_ = on_destroy_done;
}

void Chttp2ServerListener::OnAccept(void* arg, grpc_endpoint* tcp,
                                    grpc_pollset* accepting_pollset,
                                    grpc_tcp_server_acceptor* acceptor) {
  Chttp2ServerListener* self = static_cast<Chttp2ServerListener*>(arg);
  grpc_channel_args* args = self->args_;
  grpc_channel_args* args_to_destroy = nullptr;
  RefCountedPtr<grpc_server_config_fetcher::ConnectionManager>
      connection_manager;
  {
    MutexLock lock(&self->mu_);
    connection_manager = self->connection_manager_;
  }
  auto endpoint_cleanup = [&](grpc_error_handle error) {
    grpc_endpoint_shutdown(tcp, error);
    grpc_endpoint_destroy(tcp);
    gpr_free(acceptor);
  };
  if (self->server_->config_fetcher() != nullptr) {
    if (connection_manager == nullptr) {
      grpc_error_handle error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "No ConnectionManager configured. Closing connection.");
      endpoint_cleanup(error);
      return;
    }
    // TODO(yashykt): Maybe combine the following two arg modifiers into a
    // single one.
    // Make a copy of the args so as to avoid destroying the original.
    args = grpc_channel_args_copy(args);
    absl::StatusOr<grpc_channel_args*> args_result =
        connection_manager->UpdateChannelArgsForConnection(args, tcp);
    if (!args_result.ok()) {
      gpr_log(GPR_DEBUG, "Closing connection: %s",
              args_result.status().ToString().c_str());
      endpoint_cleanup(
          GRPC_ERROR_CREATE_FROM_CPP_STRING(args_result.status().ToString()));
      return;
    }
    grpc_error_handle error = GRPC_ERROR_NONE;
    args = self->args_modifier_(*args_result, &error);
    if (error != GRPC_ERROR_NONE) {
      gpr_log(GPR_DEBUG, "Closing connection: %s",
              grpc_error_std_string(error).c_str());
      endpoint_cleanup(error);
      grpc_channel_args_destroy(args);
      return;
    }
    args_to_destroy = args;
  }
  auto memory_owner = self->memory_quota_->CreateMemoryOwner(
      absl::StrCat(grpc_endpoint_get_peer(tcp), ":server_channel"));
  auto connection = memory_owner.MakeOrphanable<ActiveConnection>(
      accepting_pollset, acceptor, args, std::move(memory_owner));
  // We no longer own acceptor
  acceptor = nullptr;
  // Hold a ref to connection to allow starting handshake outside the
  // critical region
  RefCountedPtr<ActiveConnection> connection_ref = connection->Ref();
  RefCountedPtr<Chttp2ServerListener> listener_ref;
  {
    MutexLock lock(&self->mu_);
    // Shutdown the the connection if listener's stopped serving or if the
    // connection manager has changed.
    if (!self->shutdown_ && self->is_serving_ &&
        connection_manager == self->connection_manager_) {
      // This ref needs to be taken in the critical region after having made
      // sure that the listener has not been Orphaned, so as to avoid
      // heap-use-after-free issues where `Ref()` is invoked when the ref of
      // tcp_server_ has already reached 0. (Ref() implementation of
      // Chttp2ServerListener is grpc_tcp_server_ref().)
      listener_ref = self->Ref();
      self->connections_.emplace(connection.get(), std::move(connection));
    }
  }
  if (connection != nullptr) {
    endpoint_cleanup(GRPC_ERROR_NONE);
  } else {
    connection_ref->Start(std::move(listener_ref), tcp, args);
  }
  grpc_channel_args_destroy(args_to_destroy);
}

void Chttp2ServerListener::TcpServerShutdownComplete(void* arg,
                                                     grpc_error_handle error) {
  Chttp2ServerListener* self = static_cast<Chttp2ServerListener*>(arg);
  self->channelz_listen_socket_.reset();
  GRPC_ERROR_UNREF(error);
  delete self;
}

/* Server callback: destroy the tcp listener (so we don't generate further
   callbacks) */
void Chttp2ServerListener::Orphan() {
  // Cancel the watch before shutting down so as to avoid holding a ref to the
  // listener in the watcher.
  if (config_fetcher_watcher_ != nullptr) {
    server_->config_fetcher()->CancelWatch(config_fetcher_watcher_);
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
  grpc_tcp_server_shutdown_listeners(tcp_server);
  grpc_tcp_server_unref(tcp_server);
}

}  // namespace

//
// Chttp2ServerAddPort()
//

grpc_error_handle Chttp2ServerAddPort(Server* server, const char* addr,
                                      grpc_channel_args* args,
                                      Chttp2ServerArgsModifier args_modifier,
                                      int* port_num) {
  if (addr == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Invalid address: addr cannot be a nullptr.");
  }
  if (strncmp(addr, "external:", 9) == 0) {
    return Chttp2ServerListener::CreateWithAcceptor(server, addr, args,
                                                    args_modifier);
  }
  *port_num = -1;
  absl::StatusOr<std::vector<grpc_resolved_address>> resolved_or;
  std::vector<grpc_error_handle> error_list;
  std::string parsed_addr = URI::PercentDecode(addr);
  absl::string_view parsed_addr_unprefixed{parsed_addr};
  // Using lambda to avoid use of goto.
  grpc_error_handle error = [&]() {
    grpc_error_handle error = GRPC_ERROR_NONE;
    if (absl::ConsumePrefix(&parsed_addr_unprefixed, kUnixUriPrefix)) {
      resolved_or = grpc_resolve_unix_domain_address(parsed_addr_unprefixed);
    } else if (absl::ConsumePrefix(&parsed_addr_unprefixed,
                                   kUnixAbstractUriPrefix)) {
      resolved_or =
          grpc_resolve_unix_abstract_domain_address(parsed_addr_unprefixed);
    } else {
      resolved_or = GetDNSResolver()->ResolveNameBlocking(parsed_addr, "https");
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
      error = Chttp2ServerListener::Create(server, &addr,
                                           grpc_channel_args_copy(args),
                                           args_modifier, &port_temp);
      if (error != GRPC_ERROR_NONE) {
        error_list.push_back(error);
      } else {
        if (*port_num == -1) {
          *port_num = port_temp;
        } else {
          GPR_ASSERT(*port_num == port_temp);
        }
      }
    }
    if (error_list.size() == resolved_or->size()) {
      std::string msg =
          absl::StrFormat("No address added out of total %" PRIuPTR " resolved",
                          resolved_or->size());
      return GRPC_ERROR_CREATE_REFERENCING_FROM_COPIED_STRING(
          msg.c_str(), error_list.data(), error_list.size());
    } else if (!error_list.empty()) {
      std::string msg = absl::StrFormat(
          "Only %" PRIuPTR " addresses added out of total %" PRIuPTR
          " resolved",
          resolved_or->size() - error_list.size(), resolved_or->size());
      error = GRPC_ERROR_CREATE_REFERENCING_FROM_COPIED_STRING(
          msg.c_str(), error_list.data(), error_list.size());
      gpr_log(GPR_INFO, "WARNING: %s", grpc_error_std_string(error).c_str());
      GRPC_ERROR_UNREF(error);
      // we managed to bind some addresses: continue without error
    }
    return GRPC_ERROR_NONE;
  }();  // lambda end
  for (grpc_error_handle error : error_list) {
    GRPC_ERROR_UNREF(error);
  }
  grpc_channel_args_destroy(args);
  if (error != GRPC_ERROR_NONE) *port_num = 0;
  return error;
}

}  // namespace grpc_core
