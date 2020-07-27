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
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"

namespace grpc_core {
namespace {

class Chttp2ServerListener : public ServerListenerInterface {
 public:
  static grpc_error* Create(grpc_server* server, const char* addr,
                            grpc_channel_args* args, int* port_num);

  static grpc_error* CreateWithAcceptor(grpc_server* server, const char* name,
                                        grpc_channel_args* args);

  // Do not instantiate directly.  Use one of the factory methods above.
  Chttp2ServerListener(grpc_server* server, grpc_channel_args* args);
  ~Chttp2ServerListener();

  void Start(grpc_server* server,
             const std::vector<grpc_pollset*>* pollsets) override;

  channelz::ListenSocketNode* channelz_listen_socket_node() const override {
    return channelz_listen_socket_.get();
  }

  void SetOnDestroyDone(grpc_closure* on_destroy_done) override;

  void Orphan() override;

 private:
  class ConnectionState : public RefCounted<ConnectionState> {
   public:
    ConnectionState(Chttp2ServerListener* listener,
                    grpc_pollset* accepting_pollset,
                    grpc_tcp_server_acceptor* acceptor,
                    RefCountedPtr<HandshakeManager> handshake_mgr,
                    grpc_channel_args* args, grpc_endpoint* endpoint);

    ~ConnectionState();

   private:
    static void OnTimeout(void* arg, grpc_error* error);
    static void OnReceiveSettings(void* arg, grpc_error* error);
    static void OnHandshakeDone(void* arg, grpc_error* error);

    Chttp2ServerListener* const listener_;
    grpc_pollset* const accepting_pollset_;
    grpc_tcp_server_acceptor* const acceptor_;
    RefCountedPtr<HandshakeManager> handshake_mgr_;
    // State for enforcing handshake timeout on receiving HTTP/2 settings.
    grpc_chttp2_transport* transport_ = nullptr;
    grpc_millis deadline_;
    grpc_timer timer_;
    grpc_closure on_timeout_;
    grpc_closure on_receive_settings_;
    grpc_pollset_set* const interested_parties_;
  };

  static void OnAccept(void* arg, grpc_endpoint* tcp,
                       grpc_pollset* accepting_pollset,
                       grpc_tcp_server_acceptor* acceptor);

  RefCountedPtr<HandshakeManager> CreateHandshakeManager();

  static void TcpServerShutdownComplete(void* arg, grpc_error* error);

  static void DestroyListener(grpc_server* /*server*/, void* arg,
                              grpc_closure* destroy_done);

  grpc_server* const server_;
  grpc_channel_args* const args_;
  grpc_tcp_server* tcp_server_;
  Mutex mu_;
  bool shutdown_ = true;
  grpc_closure tcp_server_shutdown_complete_;
  grpc_closure* on_destroy_done_ = nullptr;
  HandshakeManager* pending_handshake_mgrs_ = nullptr;
  RefCountedPtr<channelz::ListenSocketNode> channelz_listen_socket_;
};

//
// Chttp2ServerListener::ConnectionState
//

grpc_millis GetConnectionDeadline(const grpc_channel_args* args) {
  int timeout_ms =
      grpc_channel_args_find_integer(args, GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS,
                                     {120 * GPR_MS_PER_SEC, 1, INT_MAX});
  return ExecCtx::Get()->Now() + timeout_ms;
}

Chttp2ServerListener::ConnectionState::ConnectionState(
    Chttp2ServerListener* listener, grpc_pollset* accepting_pollset,
    grpc_tcp_server_acceptor* acceptor,
    RefCountedPtr<HandshakeManager> handshake_mgr, grpc_channel_args* args,
    grpc_endpoint* endpoint)
    : listener_(listener),
      accepting_pollset_(accepting_pollset),
      acceptor_(acceptor),
      handshake_mgr_(std::move(handshake_mgr)),
      deadline_(GetConnectionDeadline(args)),
      interested_parties_(grpc_pollset_set_create()) {
  grpc_pollset_set_add_pollset(interested_parties_, accepting_pollset_);
  HandshakerRegistry::AddHandshakers(HANDSHAKER_SERVER, args,
                                     interested_parties_, handshake_mgr_.get());
  handshake_mgr_->DoHandshake(endpoint, args, deadline_, acceptor_,
                              OnHandshakeDone, this);
}

Chttp2ServerListener::ConnectionState::~ConnectionState() {
  if (transport_ != nullptr) {
    GRPC_CHTTP2_UNREF_TRANSPORT(transport_, "receive settings timeout");
  }
  grpc_pollset_set_del_pollset(interested_parties_, accepting_pollset_);
  grpc_pollset_set_destroy(interested_parties_);
}

void Chttp2ServerListener::ConnectionState::OnTimeout(void* arg,
                                                      grpc_error* error) {
  ConnectionState* self = static_cast<ConnectionState*>(arg);
  // Note that we may be called with GRPC_ERROR_NONE when the timer fires
  // or with an error indicating that the timer system is being shut down.
  if (error != GRPC_ERROR_CANCELLED) {
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->disconnect_with_error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Did not receive HTTP/2 settings before handshake timeout");
    grpc_transport_perform_op(&self->transport_->base, op);
  }
  self->Unref();
}

void Chttp2ServerListener::ConnectionState::OnReceiveSettings(
    void* arg, grpc_error* error) {
  ConnectionState* self = static_cast<ConnectionState*>(arg);
  if (error == GRPC_ERROR_NONE) {
    grpc_timer_cancel(&self->timer_);
  }
  self->Unref();
}

void Chttp2ServerListener::ConnectionState::OnHandshakeDone(void* arg,
                                                            grpc_error* error) {
  auto* args = static_cast<HandshakerArgs*>(arg);
  ConnectionState* self = static_cast<ConnectionState*>(args->user_data);
  {
    MutexLock lock(&self->listener_->mu_);
    grpc_resource_user* resource_user =
        grpc_server_get_default_resource_user(self->listener_->server_);
    if (error != GRPC_ERROR_NONE || self->listener_->shutdown_) {
      const char* error_str = grpc_error_string(error);
      gpr_log(GPR_DEBUG, "Handshaking failed: %s", error_str);
      grpc_resource_user* resource_user =
          grpc_server_get_default_resource_user(self->listener_->server_);
      if (resource_user != nullptr) {
        grpc_resource_user_free(resource_user,
                                GRPC_RESOURCE_QUOTA_CHANNEL_SIZE);
      }
      if (error == GRPC_ERROR_NONE && args->endpoint != nullptr) {
        // We were shut down after handshaking completed successfully, so
        // destroy the endpoint here.
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
        grpc_server_setup_transport(
            self->listener_->server_, transport, self->accepting_pollset_,
            args->args, grpc_chttp2_transport_get_socket_node(transport),
            resource_user);
        // Use notify_on_receive_settings callback to enforce the
        // handshake deadline.
        // Note: The reinterpret_cast<>s here are safe, because
        // grpc_chttp2_transport is a C-style extension of
        // grpc_transport, so this is morally equivalent of a
        // static_cast<> to a derived class.
        // TODO(roth): Change to static_cast<> when we C++-ify the
        // transport API.
        self->transport_ = reinterpret_cast<grpc_chttp2_transport*>(transport);
        self->Ref().release();  // Held by OnReceiveSettings().
        GRPC_CLOSURE_INIT(&self->on_receive_settings_, OnReceiveSettings, self,
                          grpc_schedule_on_exec_ctx);
        grpc_chttp2_transport_start_reading(transport, args->read_buffer,
                                            &self->on_receive_settings_);
        grpc_channel_args_destroy(args->args);
        self->Ref().release();  // Held by OnTimeout().
        GRPC_CHTTP2_REF_TRANSPORT(
            reinterpret_cast<grpc_chttp2_transport*>(transport),
            "receive settings timeout");
        GRPC_CLOSURE_INIT(&self->on_timeout_, OnTimeout, self,
                          grpc_schedule_on_exec_ctx);
        grpc_timer_init(&self->timer_, self->deadline_, &self->on_timeout_);
      } else {
        if (resource_user != nullptr) {
          grpc_resource_user_free(resource_user,
                                  GRPC_RESOURCE_QUOTA_CHANNEL_SIZE);
        }
      }
    }
    self->handshake_mgr_->RemoveFromPendingMgrList(
        &self->listener_->pending_handshake_mgrs_);
  }
  self->handshake_mgr_.reset();
  gpr_free(self->acceptor_);
  grpc_tcp_server_unref(self->listener_->tcp_server_);
  self->Unref();
}

//
// Chttp2ServerListener
//

grpc_error* Chttp2ServerListener::Create(grpc_server* server, const char* addr,
                                         grpc_channel_args* args,
                                         int* port_num) {
  std::vector<grpc_error*> error_list;
  grpc_resolved_addresses* resolved = nullptr;
  Chttp2ServerListener* listener = nullptr;
  // The bulk of this method is inside of a lambda to make cleanup
  // easier without using goto.
  grpc_error* error = [&]() {
    *port_num = -1;
    /* resolve address */
    grpc_error* error = grpc_blocking_resolve_address(addr, "https", &resolved);
    if (error != GRPC_ERROR_NONE) return error;
    // Create Chttp2ServerListener.
    listener = new Chttp2ServerListener(server, args);
    error = grpc_tcp_server_create(&listener->tcp_server_shutdown_complete_,
                                   args, &listener->tcp_server_);
    if (error != GRPC_ERROR_NONE) return error;
    for (size_t i = 0; i < resolved->naddrs; i++) {
      int port_temp;
      error = grpc_tcp_server_add_port(listener->tcp_server_,
                                       &resolved->addrs[i], &port_temp);
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
      /* we managed to bind some addresses: continue */
    }
    // Create channelz node.
    if (grpc_channel_args_find_bool(args, GRPC_ARG_ENABLE_CHANNELZ,
                                    GRPC_ENABLE_CHANNELZ_DEFAULT)) {
      listener->channelz_listen_socket_ =
          MakeRefCounted<channelz::ListenSocketNode>(
              addr, absl::StrFormat("chttp2 listener %s", addr));
    }
    /* Register with the server only upon success */
    grpc_server_add_listener(server,
                             OrphanablePtr<ServerListenerInterface>(listener));
    return GRPC_ERROR_NONE;
  }();
  if (resolved != nullptr) {
    grpc_resolved_addresses_destroy(resolved);
  }
  if (error != GRPC_ERROR_NONE) {
    if (listener != nullptr) {
      if (listener->tcp_server_ != nullptr) {
        grpc_tcp_server_unref(listener->tcp_server_);
      } else {
        delete listener;
      }
    } else {
      grpc_channel_args_destroy(args);
    }
    *port_num = 0;
  }
  for (grpc_error* error : error_list) {
    GRPC_ERROR_UNREF(error);
  }
  return error;
}

grpc_error* Chttp2ServerListener::CreateWithAcceptor(grpc_server* server,
                                                     const char* name,
                                                     grpc_channel_args* args) {
  Chttp2ServerListener* listener = new Chttp2ServerListener(server, args);
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
  grpc_server_add_listener(server,
                           OrphanablePtr<ServerListenerInterface>(listener));
  return GRPC_ERROR_NONE;
}

Chttp2ServerListener::Chttp2ServerListener(grpc_server* server,
                                           grpc_channel_args* args)
    : server_(server), args_(args) {
  GRPC_CLOSURE_INIT(&tcp_server_shutdown_complete_, TcpServerShutdownComplete,
                    this, grpc_schedule_on_exec_ctx);
}

Chttp2ServerListener::~Chttp2ServerListener() {
  grpc_channel_args_destroy(args_);
}

/* Server callback: start listening on our ports */
void Chttp2ServerListener::Start(grpc_server* /*server*/,
                                 const std::vector<grpc_pollset*>* pollsets) {
  {
    MutexLock lock(&mu_);
    shutdown_ = false;
  }
  grpc_tcp_server_start(tcp_server_, pollsets, OnAccept, this);
}

void Chttp2ServerListener::SetOnDestroyDone(grpc_closure* on_destroy_done) {
  MutexLock lock(&mu_);
  on_destroy_done_ = on_destroy_done;
}

RefCountedPtr<HandshakeManager> Chttp2ServerListener::CreateHandshakeManager() {
  MutexLock lock(&mu_);
  if (shutdown_) return nullptr;
  grpc_resource_user* resource_user =
      grpc_server_get_default_resource_user(server_);
  if (resource_user != nullptr &&
      !grpc_resource_user_safe_alloc(resource_user,
                                     GRPC_RESOURCE_QUOTA_CHANNEL_SIZE)) {
    gpr_log(GPR_ERROR,
            "Memory quota exhausted, rejecting connection, no handshaking.");
    return nullptr;
  }
  auto handshake_mgr = MakeRefCounted<HandshakeManager>();
  handshake_mgr->AddToPendingMgrList(&pending_handshake_mgrs_);
  grpc_tcp_server_ref(tcp_server_);  // Ref held by ConnectionState.
  return handshake_mgr;
}

void Chttp2ServerListener::OnAccept(void* arg, grpc_endpoint* tcp,
                                    grpc_pollset* accepting_pollset,
                                    grpc_tcp_server_acceptor* acceptor) {
  Chttp2ServerListener* self = static_cast<Chttp2ServerListener*>(arg);
  RefCountedPtr<HandshakeManager> handshake_mgr =
      self->CreateHandshakeManager();
  if (handshake_mgr == nullptr) {
    grpc_endpoint_shutdown(tcp, GRPC_ERROR_NONE);
    grpc_endpoint_destroy(tcp);
    gpr_free(acceptor);
    return;
  }
  // Deletes itself when done.
  new ConnectionState(self, accepting_pollset, acceptor,
                      std::move(handshake_mgr), self->args_, tcp);
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
    if (self->pending_handshake_mgrs_ != nullptr) {
      self->pending_handshake_mgrs_->ShutdownAllPending(GRPC_ERROR_REF(error));
    }
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

grpc_error* Chttp2ServerAddPort(grpc_server* server, const char* addr,
                                grpc_channel_args* args, int* port_num) {
  if (strncmp(addr, "external:", 9) == 0) {
    return grpc_core::Chttp2ServerListener::CreateWithAcceptor(server, addr,
                                                               args);
  }
  return grpc_core::Chttp2ServerListener::Create(server, addr, args, port_num);
}

}  // namespace grpc_core
