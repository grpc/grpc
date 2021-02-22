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

#include <grpc/grpc.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resource_quota.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"

namespace grpc_core {
namespace {

const char kUnixUriPrefix[] = "unix:";
const char kUnixAbstractUriPrefix[] = "unix-abstract:";

class Chttp2ServerListener : public Server::ListenerInterface {
 public:
  static grpc_error* Create(Server* server, grpc_resolved_address* addr,
                            grpc_channel_args* args,
                            Chttp2ServerArgsModifier args_modifier,
                            int* port_num);

  static grpc_error* CreateWithAcceptor(Server* server, const char* name,
                                        grpc_channel_args* args,
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
    explicit ConfigFetcherWatcher(Chttp2ServerListener* listener)
        : listener_(listener) {}

    void UpdateConfig(grpc_channel_args* args) override {
      {
        MutexLock lock(&listener_->mu_);
        grpc_channel_args_destroy(listener_->args_);
        grpc_error* error = GRPC_ERROR_NONE;
        args = listener_->args_modifier_(args, &error);
        if (error != GRPC_ERROR_NONE) {
          // TODO(yashykt): Set state to close down connections immediately
          // after accepting.
          GPR_ASSERT(0);
        }
        listener_->args_ = args;
        listener_->is_serving_ = true;
        if (!listener_->shutdown_) return;  // Already started listening.
      }
      int port_temp;
      grpc_error* error = grpc_tcp_server_add_port(
          listener_->tcp_server_, &listener_->resolved_address_, &port_temp);
      if (error != GRPC_ERROR_NONE) {
        GRPC_ERROR_UNREF(error);
        gpr_log(GPR_ERROR, "Error adding port to server: %s",
                grpc_error_string(error));
        // TODO(yashykt): We wouldn't need to assert here if we bound to the
        // port earlier during AddPort.
        GPR_ASSERT(0);
      }
      listener_->StartListening();
    }

    void StopServing() override {
      MutexLock lock(&listener_->mu_);
      listener_->StopServingLocked();
    }

   private:
    Chttp2ServerListener* listener_;
  };

  class ActiveConnection : public RefCounted<ActiveConnection> {
   public:
    class HandshakingState : public RefCounted<HandshakingState> {
     public:
      HandshakingState(RefCountedPtr<ActiveConnection> connection,
                       grpc_pollset* accepting_pollset,
                       grpc_tcp_server_acceptor* acceptor,
                       grpc_channel_args* args, grpc_endpoint* endpoint);

      ~HandshakingState() override;

     private:
      static void OnTimeout(void* arg, grpc_error* error);
      static void OnReceiveSettings(void* arg, grpc_error* error);
      static void OnHandshakeDone(void* arg, grpc_error* error);
      RefCountedPtr<ActiveConnection> connection_;
      grpc_pollset* const accepting_pollset_;
      grpc_tcp_server_acceptor* const acceptor_;
      // State for enforcing handshake timeout on receiving HTTP/2 settings.
      grpc_millis deadline_;
      grpc_timer timer_;
      grpc_closure on_timeout_;
      grpc_closure on_receive_settings_;
      grpc_pollset_set* const interested_parties_;
    };

    ActiveConnection(Chttp2ServerListener* listener,
                     grpc_pollset* accepting_pollset,
                     grpc_tcp_server_acceptor* acceptor,
                     grpc_channel_args* args, grpc_endpoint* endpoint);
    // The analyzer is not able to recognize that mu_ being held is the same as
    // listener_->mu_
    void StopServingLocked() ABSL_NO_THREAD_SAFETY_ANALYSIS;

   private:
    static void OnClose(void* arg, grpc_error* error);

    // A ref is held to listener_->tcp_server_ to make sure that the listener_
    // does not go away before we expect.
    Chttp2ServerListener* const listener_;
    RefCountedPtr<HandshakeManager> handshake_mgr_
        ABSL_GUARDED_BY(listener_->mu_);
    // Guarding with atomics instead of mutex to avoid running into deadlocks.
    // It is updated once with Acquire-Release semantics when the handshake
    // succeeds.
    Atomic<grpc_chttp2_transport*> transport_;
    grpc_closure on_close_;
    bool is_serving_ ABSL_GUARDED_BY(listener_->mu_) = true;
  };

  // Should only be called once so as to start the TCP server.
  void StartListening();
  // Results in all currently active connections stopping to serve new requests.
  void StopServingLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  static void OnAccept(void* arg, grpc_endpoint* tcp,
                       grpc_pollset* accepting_pollset,
                       grpc_tcp_server_acceptor* acceptor);

  // Starts the handshake. Returns true on success, false otherwise.
  bool StartHandshake(grpc_endpoint* tcp, grpc_pollset* accepting_pollset,
                      grpc_tcp_server_acceptor* acceptor);

  static void TcpServerShutdownComplete(void* arg, grpc_error* error);

  static void DestroyListener(Server* /*server*/, void* arg,
                              grpc_closure* destroy_done);

  Server* const server_;
  grpc_tcp_server* tcp_server_;
  grpc_resolved_address resolved_address_;
  Chttp2ServerArgsModifier args_modifier_;
  ConfigFetcherWatcher* config_fetcher_watcher_ = nullptr;
  Mutex mu_;
  grpc_channel_args* args_ ABSL_GUARDED_BY(mu_);
  bool is_serving_ ABSL_GUARDED_BY(mu_) = false;
  bool shutdown_ ABSL_GUARDED_BY(mu_) = true;
  std::set<ActiveConnection*> connections_ ABSL_GUARDED_BY(mu_);
  grpc_closure tcp_server_shutdown_complete_ ABSL_GUARDED_BY(mu_);
  grpc_closure* on_destroy_done_ ABSL_GUARDED_BY(mu_) = nullptr;
  RefCountedPtr<channelz::ListenSocketNode> channelz_listen_socket_;
};

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
    RefCountedPtr<ActiveConnection> connection, grpc_pollset* accepting_pollset,
    grpc_tcp_server_acceptor* acceptor, grpc_channel_args* args,
    grpc_endpoint* endpoint)
    : connection_(std::move(connection)),
      accepting_pollset_(accepting_pollset),
      acceptor_(acceptor),
      deadline_(GetConnectionDeadline(args)),
      interested_parties_(grpc_pollset_set_create()) {
  grpc_pollset_set_add_pollset(interested_parties_, accepting_pollset_);
  auto handshake_mgr = MakeRefCounted<HandshakeManager>();
  HandshakerRegistry::AddHandshakers(HANDSHAKER_SERVER, args,
                                     interested_parties_, handshake_mgr.get());
  handshake_mgr->DoHandshake(endpoint, args, deadline_, acceptor_,
                             OnHandshakeDone, this);
  MutexLock lock(&connection_->listener_->mu_);
  // If the listener's stopped serving, then shutdown the handshake early.
  if (connection_->listener_->shutdown_ ||
      !connection_->listener_->is_serving_) {
    handshake_mgr->Shutdown(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Listener stopped serving"));
  }
  connection_->handshake_mgr_ = std::move(handshake_mgr);
}

Chttp2ServerListener::ActiveConnection::HandshakingState::~HandshakingState() {
  auto* transport = connection_->transport_.Load(MemoryOrder::RELAXED);
  if (transport != nullptr) {
    GRPC_CHTTP2_UNREF_TRANSPORT(transport, "receive settings timeout");
  }
  grpc_pollset_set_del_pollset(interested_parties_, accepting_pollset_);
  grpc_pollset_set_destroy(interested_parties_);
}

void Chttp2ServerListener::ActiveConnection::HandshakingState::OnTimeout(
    void* arg, grpc_error* error) {
  HandshakingState* self = static_cast<HandshakingState*>(arg);
  // Note that we may be called with GRPC_ERROR_NONE when the timer fires
  // or with an error indicating that the timer system is being shut down.
  if (error != GRPC_ERROR_CANCELLED) {
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->disconnect_with_error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Did not receive HTTP/2 settings before handshake timeout");
    grpc_transport_perform_op(
        &self->connection_->transport_.Load(MemoryOrder::RELAXED)->base, op);
  }
  self->Unref();
}

void Chttp2ServerListener::ActiveConnection::HandshakingState::
    OnReceiveSettings(void* arg, grpc_error* error) {
  HandshakingState* self = static_cast<HandshakingState*>(arg);
  if (error == GRPC_ERROR_NONE) {
    grpc_timer_cancel(&self->timer_);
  }
  self->Unref();
}

void Chttp2ServerListener::ActiveConnection::HandshakingState::OnHandshakeDone(
    void* arg, grpc_error* error) {
  auto* args = static_cast<HandshakerArgs*>(arg);
  HandshakingState* self = static_cast<HandshakingState*>(args->user_data);
  RefCountedPtr<HandshakeManager> handshake_mgr;
  {
    MutexLock lock(&self->connection_->listener_->mu_);
    grpc_resource_user* resource_user =
        self->connection_->listener_->server_->default_resource_user();
    auto connection_cleanup_fn = [&]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(
                                     self->connection_->listener_->mu_) {
      self->connection_->listener_->connections_.erase(self->connection_.get());
      if (resource_user != nullptr) {
        grpc_resource_user_free(resource_user,
                                GRPC_RESOURCE_QUOTA_CHANNEL_SIZE);
      }
    };
    if (error != GRPC_ERROR_NONE || self->connection_->listener_->shutdown_ ||
        !self->connection_->listener_->is_serving_) {
      const char* error_str = grpc_error_string(error);
      gpr_log(GPR_DEBUG, "Handshaking failed: %s", error_str);
      connection_cleanup_fn();
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
        grpc_transport* transport = grpc_create_chttp2_transport(
            args->args, args->endpoint, false, resource_user);
        grpc_error* channel_init_err =
            self->connection_->listener_->server_->SetupTransport(
                transport, self->accepting_pollset_, args->args,
                grpc_chttp2_transport_get_socket_node(transport),
                resource_user);
        if (channel_init_err == GRPC_ERROR_NONE) {
          // Use notify_on_receive_settings callback to enforce the
          // handshake deadline.
          // Note: The reinterpret_cast<>s here are safe, because
          // grpc_chttp2_transport is a C-style extension of
          // grpc_transport, so this is morally equivalent of a
          // static_cast<> to a derived class.
          // TODO(roth): Change to static_cast<> when we C++-ify the
          // transport API.
          self->connection_->transport_.Store(
              reinterpret_cast<grpc_chttp2_transport*>(transport),
              MemoryOrder::RELEASE);
          self->Ref().release();  // Held by OnReceiveSettings().
          GRPC_CLOSURE_INIT(&self->on_receive_settings_, OnReceiveSettings,
                            self, grpc_schedule_on_exec_ctx);
          // Refs helds by OnClose()
          self->connection_->Ref().release();
          grpc_tcp_server_ref(self->connection_->listener_->tcp_server_);
          GRPC_CHTTP2_REF_TRANSPORT(
              self->connection_->transport_.Load(MemoryOrder::RELAXED),
              "on close");
          GRPC_CLOSURE_INIT(&self->connection_->on_close_,
                            ActiveConnection::OnClose, self->connection_.get(),
                            grpc_schedule_on_exec_ctx);
          grpc_chttp2_transport_start_reading(transport, args->read_buffer,
                                              &self->on_receive_settings_,
                                              &self->connection_->on_close_);
          grpc_channel_args_destroy(args->args);
          self->Ref().release();  // Held by OnTimeout().
          GRPC_CHTTP2_REF_TRANSPORT(
              reinterpret_cast<grpc_chttp2_transport*>(transport),
              "receive settings timeout");
          GRPC_CLOSURE_INIT(&self->on_timeout_, OnTimeout, self,
                            grpc_schedule_on_exec_ctx);
          grpc_timer_init(&self->timer_, self->deadline_, &self->on_timeout_);
        } else {
          // Failed to create channel from transport. Clean up.
          gpr_log(GPR_ERROR, "Failed to create channel: %s",
                  grpc_error_string(channel_init_err));
          GRPC_ERROR_UNREF(channel_init_err);
          grpc_transport_destroy(transport);
          grpc_slice_buffer_destroy_internal(args->read_buffer);
          gpr_free(args->read_buffer);
          connection_cleanup_fn();
          grpc_channel_args_destroy(args->args);
        }
      } else {
        connection_cleanup_fn();
      }
    }
    // Avoid calling the destructor from within the locked region
    handshake_mgr = std::move(self->connection_->handshake_mgr_);
  }
  handshake_mgr.reset();
  gpr_free(self->acceptor_);
  grpc_tcp_server_unref(self->connection_->listener_->tcp_server_);
  self->Unref();
}

//
// Chttp2ServerListener::ActiveConnection
//

Chttp2ServerListener::ActiveConnection::ActiveConnection(
    Chttp2ServerListener* listener, grpc_pollset* accepting_pollset,
    grpc_tcp_server_acceptor* acceptor, grpc_channel_args* args,
    grpc_endpoint* endpoint)
    : listener_(listener) {
  {
    MutexLock lock(&listener_->mu_);
    grpc_tcp_server_ref(
        listener_->tcp_server_);  // Ref held by HandshakingState.
    listener_->connections_.insert(this);
  }
  // Deletes itself when done.
  new ActiveConnection::HandshakingState(this, accepting_pollset, acceptor,
                                         args, endpoint);
}

void Chttp2ServerListener::ActiveConnection::StopServingLocked() {
  is_serving_ = false;
  if (handshake_mgr_ != nullptr) {
    handshake_mgr_->Shutdown(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Listener stopped serving"));
  } else {
    auto* transport = transport_.Load(MemoryOrder::RELAXED);
    if (transport != nullptr) {
      grpc_transport_op* op = grpc_make_transport_op(nullptr);
      op->goaway_error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Server is stopping to serve requests.");
      grpc_transport_perform_op(&transport->base, op);
    }
  }
}

void Chttp2ServerListener::ActiveConnection::OnClose(void* arg,
                                                     grpc_error* /* error */) {
  ActiveConnection* self = static_cast<ActiveConnection*>(arg);
  {
    MutexLock lock(&self->listener_->mu_);
    // The node was already deleted from the connections_ list if the connection
    // is not serving.
    if (self->is_serving_) {
      self->listener_->connections_.erase(self);
    }
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(self->transport_.Load(MemoryOrder::RELAXED),
                              "on close");
  grpc_tcp_server_unref(self->listener_->tcp_server_);
  self->Unref();
}

//
// Chttp2ServerListener
//

grpc_error* Chttp2ServerListener::Create(Server* server,
                                         grpc_resolved_address* addr,
                                         grpc_channel_args* args,
                                         Chttp2ServerArgsModifier args_modifier,
                                         int* port_num) {
  Chttp2ServerListener* listener = nullptr;
  // The bulk of this method is inside of a lambda to make cleanup
  // easier without using goto.
  grpc_error* error = [&]() {
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
      std::string string_address = grpc_sockaddr_to_string(addr, false);
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

grpc_error* Chttp2ServerListener::CreateWithAcceptor(
    Server* server, const char* name, grpc_channel_args* args,
    Chttp2ServerArgsModifier args_modifier) {
  Chttp2ServerListener* listener =
      new Chttp2ServerListener(server, args, args_modifier);
  grpc_error* error = grpc_tcp_server_create(
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
    : server_(server), args_modifier_(args_modifier), args_(args) {
  GRPC_CLOSURE_INIT(&tcp_server_shutdown_complete_, TcpServerShutdownComplete,
                    this, grpc_schedule_on_exec_ctx);
}

Chttp2ServerListener::~Chttp2ServerListener() {
  grpc_channel_args_destroy(args_);
}

/* Server callback: start listening on our ports */
void Chttp2ServerListener::Start(
    Server* /*server*/, const std::vector<grpc_pollset*>* /* pollsets */) {
  if (server_->config_fetcher() != nullptr) {
    grpc_channel_args* args = nullptr;
    auto watcher = absl::make_unique<ConfigFetcherWatcher>(this);
    config_fetcher_watcher_ = watcher.get();
    {
      MutexLock lock(&mu_);
      args = grpc_channel_args_copy(args_);
    }
    server_->config_fetcher()->StartWatch(
        grpc_sockaddr_to_string(&resolved_address_, false), args,
        std::move(watcher));
  } else {
    {
      MutexLock lock(&mu_);
      is_serving_ = true;
    }
    StartListening();
  }
}

void Chttp2ServerListener::StartListening() {
  grpc_tcp_server_start(tcp_server_, &server_->pollsets(), OnAccept, this);
  MutexLock lock(&mu_);
  shutdown_ = false;
}

void Chttp2ServerListener::StopServingLocked() {
  is_serving_ = false;
  for (ActiveConnection* connection : connections_) {
    connection->StopServingLocked();
  }
  connections_.clear();
}

void Chttp2ServerListener::SetOnDestroyDone(grpc_closure* on_destroy_done) {
  MutexLock lock(&mu_);
  on_destroy_done_ = on_destroy_done;
}

bool Chttp2ServerListener::StartHandshake(grpc_endpoint* tcp,
                                          grpc_pollset* accepting_pollset,
                                          grpc_tcp_server_acceptor* acceptor) {
  RefCountedPtr<HandshakeManager> handshake_mgr;
  grpc_channel_args* args = nullptr;
  {
    MutexLock lock(&mu_);
    if (!is_serving_ || shutdown_) return false;
    grpc_resource_user* resource_user = server_->default_resource_user();
    if (resource_user != nullptr &&
        !grpc_resource_user_safe_alloc(resource_user,
                                       GRPC_RESOURCE_QUOTA_CHANNEL_SIZE)) {
      gpr_log(GPR_ERROR,
              "Memory quota exhausted, rejecting connection, no handshaking.");
      return false;
    }
    args = grpc_channel_args_copy(args_);
  }
  // Deletes itself when done.
  new ActiveConnection(this, accepting_pollset, acceptor, args, tcp);
  grpc_channel_args_destroy(args);
  return true;
}

void Chttp2ServerListener::OnAccept(void* arg, grpc_endpoint* tcp,
                                    grpc_pollset* accepting_pollset,
                                    grpc_tcp_server_acceptor* acceptor) {
  Chttp2ServerListener* self = static_cast<Chttp2ServerListener*>(arg);
  if (!self->StartHandshake(tcp, accepting_pollset, acceptor)) {
    grpc_endpoint_shutdown(tcp, GRPC_ERROR_NONE);
    grpc_endpoint_destroy(tcp);
    gpr_free(acceptor);
  }
}

void Chttp2ServerListener::TcpServerShutdownComplete(void* arg,
                                                     grpc_error* error) {
  Chttp2ServerListener* self = static_cast<Chttp2ServerListener*>(arg);
  /* ensure all threads have unlocked */
  grpc_closure* destroy_done = nullptr;
  {
    MutexLock lock(&self->mu_);
    destroy_done = self->on_destroy_done_;
    GPR_ASSERT(self->shutdown_);
    self->StopServingLocked();
    self->channelz_listen_socket_.reset();
  }
  // Flush queued work before destroying handshaker factory, since that
  // may do a synchronous unref.
  ExecCtx::Get()->Flush();
  if (destroy_done != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, destroy_done, GRPC_ERROR_REF(error));
    ExecCtx::Get()->Flush();
  }
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
  grpc_tcp_server* tcp_server;
  {
    MutexLock lock(&mu_);
    shutdown_ = true;
    tcp_server = tcp_server_;
  }
  grpc_tcp_server_shutdown_listeners(tcp_server);
  grpc_tcp_server_unref(tcp_server);
}

}  // namespace

//
// Chttp2ServerAddPort()
//

grpc_error* Chttp2ServerAddPort(Server* server, const char* addr,
                                grpc_channel_args* args,
                                Chttp2ServerArgsModifier args_modifier,
                                int* port_num) {
  if (strncmp(addr, "external:", 9) == 0) {
    return grpc_core::Chttp2ServerListener::CreateWithAcceptor(
        server, addr, args, args_modifier);
  }
  *port_num = -1;
  grpc_resolved_addresses* resolved = nullptr;
  std::vector<grpc_error*> error_list;
  // Using lambda to avoid use of goto.
  grpc_error* error = [&]() {
    if (absl::StartsWith(addr, kUnixUriPrefix)) {
      error = grpc_resolve_unix_domain_address(
          addr + sizeof(kUnixUriPrefix) - 1, &resolved);
    } else if (absl::StartsWith(addr, kUnixAbstractUriPrefix)) {
      error = grpc_resolve_unix_abstract_domain_address(
          addr + sizeof(kUnixAbstractUriPrefix) - 1, &resolved);
    } else {
      error = grpc_blocking_resolve_address(addr, "https", &resolved);
    }
    if (error != GRPC_ERROR_NONE) return error;
    // Create a listener for each resolved address.
    for (size_t i = 0; i < resolved->naddrs; i++) {
      // If address has a wildcard port (0), use the same port as a previous
      // listener.
      if (*port_num != -1 && grpc_sockaddr_get_port(&resolved->addrs[i]) == 0) {
        grpc_sockaddr_set_port(&resolved->addrs[i], *port_num);
      }
      int port_temp = -1;
      error = grpc_core::Chttp2ServerListener::Create(
          server, &resolved->addrs[i], grpc_channel_args_copy(args),
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
    if (error_list.size() == resolved->naddrs) {
      std::string msg =
          absl::StrFormat("No address added out of total %" PRIuPTR " resolved",
                          resolved->naddrs);
      return GRPC_ERROR_CREATE_REFERENCING_FROM_COPIED_STRING(
          msg.c_str(), error_list.data(), error_list.size());
    } else if (!error_list.empty()) {
      std::string msg = absl::StrFormat(
          "Only %" PRIuPTR " addresses added out of total %" PRIuPTR
          " resolved",
          resolved->naddrs - error_list.size(), resolved->naddrs);
      error = GRPC_ERROR_CREATE_REFERENCING_FROM_COPIED_STRING(
          msg.c_str(), error_list.data(), error_list.size());
      gpr_log(GPR_INFO, "WARNING: %s", grpc_error_string(error));
      GRPC_ERROR_UNREF(error);
      // we managed to bind some addresses: continue without error
    }
    return GRPC_ERROR_NONE;
  }();  // lambda end
  for (grpc_error* error : error_list) {
    GRPC_ERROR_UNREF(error);
  }
  grpc_channel_args_destroy(args);
  if (resolved != nullptr) {
    grpc_resolved_addresses_destroy(resolved);
  }
  if (error != GRPC_ERROR_NONE) *port_num = 0;
  return error;
}

}  // namespace grpc_core
