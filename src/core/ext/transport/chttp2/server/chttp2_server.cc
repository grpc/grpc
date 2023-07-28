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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/server/chttp2_server.h"

#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
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
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/iomgr/vsock.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/insecure/insecure_credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/handshaker.h"
#include "src/core/lib/transport/handshaker_registry.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/transport/transport_fwd.h"
#include "src/core/lib/uri/uri_parser.h"

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/tcp_client_posix.h"
#endif  // GPR_SUPPORT_CHANNELS_FROM_FD

namespace grpc_core {
namespace {

using ::grpc_event_engine::experimental::EventEngine;

const char kUnixUriPrefix[] = "unix:";
const char kUnixAbstractUriPrefix[] = "unix-abstract:";
const char kVSockUriPrefix[] = "vsock:";

class Chttp2ServerListener : public Server::ListenerInterface {
 public:
  static grpc_error_handle Create(Server* server, grpc_resolved_address* addr,
                                  const ChannelArgs& args,
                                  Chttp2ServerArgsModifier args_modifier,
                                  int* port_num);

  static grpc_error_handle CreateWithAcceptor(
      Server* server, const char* name, const ChannelArgs& args,
      Chttp2ServerArgsModifier args_modifier);

  // Do not instantiate directly.  Use one of the factory methods above.
  Chttp2ServerListener(Server* server, const ChannelArgs& args,
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
                       const ChannelArgs& args);

      ~HandshakingState() override;

      void Orphan() override;

      void Start(grpc_endpoint* endpoint, const ChannelArgs& args);

      // Needed to be able to grab an external ref in
      // ActiveConnection::Start()
      using InternallyRefCounted<HandshakingState>::Ref;

     private:
      void OnTimeout() ABSL_LOCKS_EXCLUDED(&connection_->mu_);
      static void OnReceiveSettings(void* arg, grpc_error_handle /* error */);
      static void OnHandshakeDone(void* arg, grpc_error_handle error);
      RefCountedPtr<ActiveConnection> const connection_;
      grpc_pollset* const accepting_pollset_;
      grpc_tcp_server_acceptor* acceptor_;
      RefCountedPtr<HandshakeManager> handshake_mgr_
          ABSL_GUARDED_BY(&connection_->mu_);
      // State for enforcing handshake timeout on receiving HTTP/2 settings.
      Timestamp const deadline_;
      absl::optional<EventEngine::TaskHandle> timer_handle_
          ABSL_GUARDED_BY(&connection_->mu_);
      grpc_closure on_receive_settings_ ABSL_GUARDED_BY(&connection_->mu_);
      grpc_pollset_set* const interested_parties_;
    };

    ActiveConnection(grpc_pollset* accepting_pollset,
                     grpc_tcp_server_acceptor* acceptor,
                     EventEngine* event_engine, const ChannelArgs& args,
                     MemoryOwner memory_owner);
    ~ActiveConnection() override;

    void Orphan() override;

    void SendGoAway();

    void Start(RefCountedPtr<Chttp2ServerListener> listener,
               grpc_endpoint* endpoint, const ChannelArgs& args);

    // Needed to be able to grab an external ref in
    // Chttp2ServerListener::OnAccept()
    using InternallyRefCounted<ActiveConnection>::Ref;

   private:
    static void OnClose(void* arg, grpc_error_handle error);
    void OnDrainGraceTimeExpiry() ABSL_LOCKS_EXCLUDED(&mu_);

    RefCountedPtr<Chttp2ServerListener> listener_;
    Mutex mu_ ABSL_ACQUIRED_AFTER(&listener_->mu_);
    // Set by HandshakingState before the handshaking begins and reset when
    // handshaking is done.
    OrphanablePtr<HandshakingState> handshaking_state_ ABSL_GUARDED_BY(&mu_);
    // Set by HandshakingState when handshaking is done and a valid transport
    // is created.
    RefCountedPtr<grpc_chttp2_transport> transport_ ABSL_GUARDED_BY(&mu_) =
        nullptr;
    grpc_closure on_close_;
    absl::optional<EventEngine::TaskHandle> drain_grace_timer_handle_
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

  // The interface required by RefCountedPtr<> has been manually implemented
  // here to take a ref on tcp_server_ instead. Note that, the handshaker
  // needs tcp_server_ to exist for the lifetime of the handshake since it's
  // needed by acceptor. Sharing refs between the listener and tcp_server_ is
  // just an optimization to avoid taking additional refs on the listener,
  // since TcpServerShutdownComplete already holds a ref to the listener.
  void IncrementRefCount() { grpc_tcp_server_ref(tcp_server_); }
  void IncrementRefCount(const DebugLocation& /* location */,
                         const char* /* reason */) {
    IncrementRefCount();
  }

  GRPC_MUST_USE_RESULT RefCountedPtr<Chttp2ServerListener> Ref() {
    IncrementRefCount();
    return RefCountedPtr<Chttp2ServerListener>(this);
  }
  GRPC_MUST_USE_RESULT RefCountedPtr<Chttp2ServerListener> Ref(
      const DebugLocation& /* location */, const char* /* reason */) {
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
  ChannelArgs args_;
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
  if (!error.ok()) {
    gpr_log(GPR_ERROR, "Error adding port to server: %s",
            StatusToString(error).c_str());
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
  // Send GOAWAYs on the transports so that they disconnected when existing
  // RPCs finish.
  for (auto& connection : connections) {
    connection.first->SendGoAway();
  }
}

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
    grpc_pollset* accepting_pollset, grpc_tcp_server_acceptor* acceptor,
    const ChannelArgs& args)
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
      handshake_mgr_->Shutdown(GRPC_ERROR_CREATE("Listener stopped serving."));
    }
  }
  Unref();
}

void Chttp2ServerListener::ActiveConnection::HandshakingState::Start(
    grpc_endpoint* endpoint, const ChannelArgs& channel_args) {
  Ref().release();  // Held by OnHandshakeDone
  RefCountedPtr<HandshakeManager> handshake_mgr;
  {
    MutexLock lock(&connection_->mu_);
    if (handshake_mgr_ == nullptr) return;
    handshake_mgr = handshake_mgr_;
  }
  handshake_mgr->DoHandshake(endpoint, channel_args, deadline_, acceptor_,
                             OnHandshakeDone, this);
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
    grpc_transport_perform_op(&transport->base, op);
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
    void* arg, grpc_error_handle error) {
  auto* args = static_cast<HandshakerArgs*>(arg);
  HandshakingState* self = static_cast<HandshakingState*>(args->user_data);
  OrphanablePtr<HandshakingState> handshaking_state_ref;
  RefCountedPtr<HandshakeManager> handshake_mgr;
  bool cleanup_connection = false;
  {
    MutexLock connection_lock(&self->connection_->mu_);
    if (!error.ok() || self->connection_->shutdown_) {
      std::string error_str = StatusToString(error);
      gpr_log(GPR_DEBUG, "Handshaking failed: %s", error_str.c_str());
      cleanup_connection = true;
      if (error.ok() && args->endpoint != nullptr) {
        // We were shut down or stopped serving after handshaking completed
        // successfully, so destroy the endpoint here.
        // TODO(ctiller): It is currently necessary to shutdown endpoints
        // before destroying them, even if we know that there are no
        // pending read/write callbacks.  This should be fixed, at which
        // point this can be removed.
        grpc_endpoint_shutdown(args->endpoint, absl::OkStatus());
        grpc_endpoint_destroy(args->endpoint);
        grpc_slice_buffer_destroy(args->read_buffer);
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
        if (channel_init_err.ok()) {
          // Use notify_on_receive_settings callback to enforce the
          // handshake deadline.
          // Note: The reinterpret_cast<>s here are safe, because
          // grpc_chttp2_transport is a C-style extension of
          // grpc_transport, so this is morally equivalent of a
          // static_cast<> to a derived class.
          // TODO(roth): Change to static_cast<> when we C++-ify the
          // transport API.
          self->connection_->transport_ =
              reinterpret_cast<grpc_chttp2_transport*>(transport)->Ref();
          self->Ref().release();  // Held by OnReceiveSettings().
          GRPC_CLOSURE_INIT(&self->on_receive_settings_, OnReceiveSettings,
                            self, grpc_schedule_on_exec_ctx);
          // If the listener has been configured with a config fetcher, we
          // need to watch on the transport being closed so that we can an
          // updated list of active connections.
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
          self->timer_handle_ = self->connection_->event_engine_->RunAfter(
              self->deadline_ - Timestamp::Now(),
              [self = self->Ref()]() mutable {
                ApplicationCallbackExecCtx callback_exec_ctx;
                ExecCtx exec_ctx;
                self->OnTimeout();
                // HandshakingState deletion might require an active ExecCtx.
                self.reset();
              });
        } else {
          // Failed to create channel from transport. Clean up.
          gpr_log(GPR_ERROR, "Failed to create channel: %s",
                  StatusToString(channel_init_err).c_str());
          grpc_transport_destroy(transport);
          grpc_slice_buffer_destroy(args->read_buffer);
          gpr_free(args->read_buffer);
          cleanup_connection = true;
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
    EventEngine* event_engine, const ChannelArgs& args,
    MemoryOwner memory_owner)
    : handshaking_state_(memory_owner.MakeOrphanable<HandshakingState>(
          Ref(), accepting_pollset, acceptor, args)),
      event_engine_(event_engine) {
  GRPC_CLOSURE_INIT(&on_close_, ActiveConnection::OnClose, this,
                    grpc_schedule_on_exec_ctx);
}

Chttp2ServerListener::ActiveConnection::~ActiveConnection() {}

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
      transport = transport_.get();
      drain_grace_timer_handle_ = event_engine_->RunAfter(
          std::max(Duration::Zero(),
                   listener_->args_
                       .GetDurationFromIntMillis(
                           GRPC_ARG_SERVER_CONFIG_CHANGE_DRAIN_GRACE_TIME_MS)
                       .value_or(Duration::Minutes(10))),
          [self = Ref(DEBUG_LOCATION, "drain_grace_timer")]() mutable {
            ApplicationCallbackExecCtx callback_exec_ctx;
            ExecCtx exec_ctx;
            self->OnDrainGraceTimeExpiry();
            self.reset(DEBUG_LOCATION, "drain_grace_timer");
          });
      shutdown_ = true;
    }
  }
  if (transport != nullptr) {
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->goaway_error =
        GRPC_ERROR_CREATE("Server is stopping to serve requests.");
    grpc_transport_perform_op(&transport->base, op);
  }
}

void Chttp2ServerListener::ActiveConnection::Start(
    RefCountedPtr<Chttp2ServerListener> listener, grpc_endpoint* endpoint,
    const ChannelArgs& args) {
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
    grpc_transport_perform_op(&transport->base, op);
  }
}

//
// Chttp2ServerListener
//

grpc_error_handle Chttp2ServerListener::Create(
    Server* server, grpc_resolved_address* addr, const ChannelArgs& args,
    Chttp2ServerArgsModifier args_modifier, int* port_num) {
  Chttp2ServerListener* listener = nullptr;
  // The bulk of this method is inside of a lambda to make cleanup
  // easier without using goto.
  grpc_error_handle error = [&]() {
    grpc_error_handle error;
    // Create Chttp2ServerListener.
    listener = new Chttp2ServerListener(server, args, args_modifier);
    error = grpc_tcp_server_create(
        &listener->tcp_server_shutdown_complete_,
        grpc_event_engine::experimental::ChannelArgsEndpointConfig(args),
        OnAccept, listener, &listener->tcp_server_);
    if (!error.ok()) return error;
    if (server->config_fetcher() != nullptr) {
      listener->resolved_address_ = *addr;
      // TODO(yashykt): Consider binding so as to be able to return the port
      // number.
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
              *string_address,
              absl::StrCat("chttp2 listener ", *string_address));
    }
    // Register with the server only upon success
    server->AddListener(OrphanablePtr<Server::ListenerInterface>(listener));
    return absl::OkStatus();
  }();
  if (!error.ok()) {
    if (listener != nullptr) {
      if (listener->tcp_server_ != nullptr) {
        // listener is deleted when tcp_server_ is shutdown.
        grpc_tcp_server_unref(listener->tcp_server_);
      } else {
        delete listener;
      }
    }
  }
  return error;
}

grpc_error_handle Chttp2ServerListener::CreateWithAcceptor(
    Server* server, const char* name, const ChannelArgs& args,
    Chttp2ServerArgsModifier args_modifier) {
  Chttp2ServerListener* listener =
      new Chttp2ServerListener(server, args, args_modifier);
  grpc_error_handle error = grpc_tcp_server_create(
      &listener->tcp_server_shutdown_complete_,
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(args),
      OnAccept, listener, &listener->tcp_server_);
  if (!error.ok()) {
    delete listener;
    return error;
  }
  // TODO(yangg) channelz
  TcpServerFdHandler** arg_val = args.GetPointer<TcpServerFdHandler*>(name);
  *arg_val = grpc_tcp_server_create_fd_handler(listener->tcp_server_);
  server->AddListener(OrphanablePtr<Server::ListenerInterface>(listener));
  return absl::OkStatus();
}

Chttp2ServerListener::Chttp2ServerListener(
    Server* server, const ChannelArgs& args,
    Chttp2ServerArgsModifier args_modifier)
    : server_(server),
      args_modifier_(args_modifier),
      args_(args),
      memory_quota_(args.GetObject<ResourceQuota>()->memory_quota()) {
  GRPC_CLOSURE_INIT(&tcp_server_shutdown_complete_, TcpServerShutdownComplete,
                    this, grpc_schedule_on_exec_ctx);
}

Chttp2ServerListener::~Chttp2ServerListener() {
  // Flush queued work before destroying handshaker factory, since that
  // may do a synchronous unref.
  ExecCtx::Get()->Flush();
  if (on_destroy_done_ != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, on_destroy_done_, absl::OkStatus());
    ExecCtx::Get()->Flush();
  }
}

// Server callback: start listening on our ports
void Chttp2ServerListener::Start(
    Server* /*server*/, const std::vector<grpc_pollset*>* /* pollsets */) {
  if (server_->config_fetcher() != nullptr) {
    auto watcher = std::make_unique<ConfigFetcherWatcher>(Ref());
    config_fetcher_watcher_ = watcher.get();
    server_->config_fetcher()->StartWatch(
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
  grpc_tcp_server_start(tcp_server_, &server_->pollsets());
}

void Chttp2ServerListener::SetOnDestroyDone(grpc_closure* on_destroy_done) {
  MutexLock lock(&mu_);
  on_destroy_done_ = on_destroy_done;
}

void Chttp2ServerListener::OnAccept(void* arg, grpc_endpoint* tcp,
                                    grpc_pollset* accepting_pollset,
                                    grpc_tcp_server_acceptor* acceptor) {
  Chttp2ServerListener* self = static_cast<Chttp2ServerListener*>(arg);
  ChannelArgs args = self->args_;
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
      grpc_error_handle error = GRPC_ERROR_CREATE(
          "No ConnectionManager configured. Closing connection.");
      endpoint_cleanup(error);
      return;
    }
    absl::StatusOr<ChannelArgs> args_result =
        connection_manager->UpdateChannelArgsForConnection(args, tcp);
    if (!args_result.ok()) {
      gpr_log(GPR_DEBUG, "Closing connection: %s",
              args_result.status().ToString().c_str());
      endpoint_cleanup(GRPC_ERROR_CREATE(args_result.status().ToString()));
      return;
    }
    grpc_error_handle error;
    args = self->args_modifier_(*args_result, &error);
    if (!error.ok()) {
      gpr_log(GPR_DEBUG, "Closing connection: %s",
              StatusToString(error).c_str());
      endpoint_cleanup(error);
      return;
    }
  }
  auto memory_owner = self->memory_quota_->CreateMemoryOwner(
      absl::StrCat(grpc_endpoint_get_peer(tcp), ":server_channel"));
  EventEngine* const event_engine = self->args_.GetObject<EventEngine>();
  auto connection = memory_owner.MakeOrphanable<ActiveConnection>(
      accepting_pollset, acceptor, event_engine, args, std::move(memory_owner));
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
    endpoint_cleanup(absl::OkStatus());
  } else {
    connection_ref->Start(std::move(listener_ref), tcp, args);
  }
}

void Chttp2ServerListener::TcpServerShutdownComplete(
    void* arg, grpc_error_handle /*error*/) {
  Chttp2ServerListener* self = static_cast<Chttp2ServerListener*>(arg);
  self->channelz_listen_socket_.reset();
  delete self;
}

// Server callback: destroy the tcp listener (so we don't generate further
// callbacks)
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
                                      const ChannelArgs& args,
                                      Chttp2ServerArgsModifier args_modifier,
                                      int* port_num) {
  if (addr == nullptr) {
    return GRPC_ERROR_CREATE("Invalid address: addr cannot be a nullptr.");
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
      error = Chttp2ServerListener::Create(server, &addr, args, args_modifier,
                                           &port_temp);
      if (!error.ok()) {
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
      gpr_log(GPR_INFO, "WARNING: %s", StatusToString(error).c_str());
      // we managed to bind some addresses: continue without error
    }
    return absl::OkStatus();
  }();  // lambda end
  if (!error.ok()) *port_num = 0;
  return error;
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
}  // namespace grpc_core

int grpc_server_add_http2_port(grpc_server* server, const char* addr,
                               grpc_server_credentials* creds) {
  grpc_core::ExecCtx exec_ctx;
  grpc_error_handle err;
  grpc_core::RefCountedPtr<grpc_server_security_connector> sc;
  int port_num = 0;
  grpc_core::Server* core_server = grpc_core::Server::FromC(server);
  grpc_core::ChannelArgs args = core_server->channel_args();
  GRPC_API_TRACE("grpc_server_add_http2_port(server=%p, addr=%s, creds=%p)", 3,
                 (server, addr, creds));
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
  err = grpc_core::Chttp2ServerAddPort(
      core_server, addr, args, grpc_core::ModifyArgsForConnection, &port_num);
done:
  sc.reset(DEBUG_LOCATION, "server");
  if (!err.ok()) {
    gpr_log(GPR_ERROR, "%s", grpc_core::StatusToString(err).c_str());
  }
  return port_num;
}

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD
void grpc_server_add_channel_from_fd(grpc_server* server, int fd,
                                     grpc_server_credentials* creds) {
  // For now, we only support insecure server credentials
  if (creds == nullptr ||
      creds->type() != grpc_core::InsecureServerCredentials::Type()) {
    gpr_log(GPR_ERROR, "Failed to create channel due to invalid creds");
    return;
  }
  grpc_core::ExecCtx exec_ctx;
  grpc_core::Server* core_server = grpc_core::Server::FromC(server);

  grpc_core::ChannelArgs server_args = core_server->channel_args();
  std::string name = absl::StrCat("fd:", fd);
  auto memory_quota =
      server_args.GetObject<grpc_core::ResourceQuota>()->memory_quota();
  grpc_endpoint* server_endpoint = grpc_tcp_create_from_fd(
      grpc_fd_create(fd, name.c_str(), true),
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(server_args),
      name);
  grpc_transport* transport = grpc_create_chttp2_transport(
      server_args, server_endpoint, false  // is_client
  );
  grpc_error_handle error =
      core_server->SetupTransport(transport, nullptr, server_args, nullptr);
  if (error.ok()) {
    for (grpc_pollset* pollset : core_server->pollsets()) {
      grpc_endpoint_add_to_pollset(server_endpoint, pollset);
    }
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
  } else {
    gpr_log(GPR_ERROR, "Failed to create channel: %s",
            grpc_core::StatusToString(error).c_str());
    grpc_transport_destroy(transport);
  }
}

#else  // !GPR_SUPPORT_CHANNELS_FROM_FD

void grpc_server_add_channel_from_fd(grpc_server* /* server */, int /* fd */,
                                     grpc_server_credentials* /* creds */) {
  GPR_ASSERT(0);
}

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD
