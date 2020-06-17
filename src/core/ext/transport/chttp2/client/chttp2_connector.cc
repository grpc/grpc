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

#include "src/core/ext/transport/chttp2/client/chttp2_connector.h"

#include <grpc/grpc.h>

#include <string.h>

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/connector.h"
#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

Chttp2Connector::Chttp2Connector() {
  GRPC_CLOSURE_INIT(&connected_, Connected, this, grpc_schedule_on_exec_ctx);
}

Chttp2Connector::~Chttp2Connector() {
  if (endpoint_ != nullptr) grpc_endpoint_destroy(endpoint_);
}

void Chttp2Connector::Connect(const Args& args, Result* result,
                              grpc_closure* notify) {
  grpc_resolved_address addr;
  Subchannel::GetAddressFromSubchannelAddressArg(args.channel_args, &addr);
  grpc_endpoint** ep;
  {
    MutexLock lock(&mu_);
    GPR_ASSERT(notify_ == nullptr);
    args_ = args;
    result_ = result;
    notify_ = notify;
    GPR_ASSERT(!connecting_);
    connecting_ = true;
    GPR_ASSERT(endpoint_ == nullptr);
    ep = &endpoint_;
  }
  // In some implementations, the closure can be flushed before
  // grpc_tcp_client_connect() returns, and since the closure requires access
  // to mu_, this can result in a deadlock (see
  // https://github.com/grpc/grpc/issues/16427 for details).
  // grpc_tcp_client_connect() will fill endpoint_ with proper contents, and we
  // make sure that we still exist at that point by taking a ref.
  Ref().release();  // Ref held by callback.
  grpc_tcp_client_connect(&connected_, ep, args.interested_parties,
                          args.channel_args, &addr, args.deadline);
}

void Chttp2Connector::Shutdown(grpc_error* error) {
  MutexLock lock(&mu_);
  shutdown_ = true;
  if (handshake_mgr_ != nullptr) {
    handshake_mgr_->Shutdown(GRPC_ERROR_REF(error));
  }
  // If handshaking is not yet in progress, shutdown the endpoint.
  // Otherwise, the handshaker will do this for us.
  if (!connecting_ && endpoint_ != nullptr) {
    grpc_endpoint_shutdown(endpoint_, GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

void Chttp2Connector::Connected(void* arg, grpc_error* error) {
  Chttp2Connector* self = static_cast<Chttp2Connector*>(arg);
  bool unref = false;
  {
    MutexLock lock(&self->mu_);
    GPR_ASSERT(self->connecting_);
    self->connecting_ = false;
    if (error != GRPC_ERROR_NONE || self->shutdown_) {
      if (error == GRPC_ERROR_NONE) {
        error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("connector shutdown");
      } else {
        error = GRPC_ERROR_REF(error);
      }
      if (self->endpoint_ != nullptr) {
        grpc_endpoint_shutdown(self->endpoint_, GRPC_ERROR_REF(error));
      }
      self->result_->Reset();
      grpc_closure* notify = self->notify_;
      self->notify_ = nullptr;
      ExecCtx::Run(DEBUG_LOCATION, notify, error);
      unref = true;
    } else {
      GPR_ASSERT(self->endpoint_ != nullptr);
      self->StartHandshakeLocked();
    }
  }
  if (unref) self->Unref();
}

void Chttp2Connector::StartHandshakeLocked() {
  handshake_mgr_ = MakeRefCounted<HandshakeManager>();
  HandshakerRegistry::AddHandshakers(HANDSHAKER_CLIENT, args_.channel_args,
                                     args_.interested_parties,
                                     handshake_mgr_.get());
  grpc_endpoint_add_to_pollset_set(endpoint_, args_.interested_parties);
  handshake_mgr_->DoHandshake(endpoint_, args_.channel_args, args_.deadline,
                              nullptr /* acceptor */, OnHandshakeDone, this);
  endpoint_ = nullptr;  // Endpoint handed off to handshake manager.
}

void Chttp2Connector::OnHandshakeDone(void* arg, grpc_error* error) {
  auto* args = static_cast<HandshakerArgs*>(arg);
  Chttp2Connector* self = static_cast<Chttp2Connector*>(args->user_data);
  {
    MutexLock lock(&self->mu_);
    if (error != GRPC_ERROR_NONE || self->shutdown_) {
      if (error == GRPC_ERROR_NONE) {
        error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("connector shutdown");
        // We were shut down after handshaking completed successfully, so
        // destroy the endpoint here.
        if (args->endpoint != nullptr) {
          // TODO(ctiller): It is currently necessary to shutdown endpoints
          // before destroying them, even if we know that there are no
          // pending read/write callbacks.  This should be fixed, at which
          // point this can be removed.
          grpc_endpoint_shutdown(args->endpoint, GRPC_ERROR_REF(error));
          grpc_endpoint_destroy(args->endpoint);
          grpc_channel_args_destroy(args->args);
          grpc_slice_buffer_destroy_internal(args->read_buffer);
          gpr_free(args->read_buffer);
        }
      } else {
        error = GRPC_ERROR_REF(error);
      }
      self->result_->Reset();
    } else if (args->endpoint != nullptr) {
      grpc_endpoint_delete_from_pollset_set(args->endpoint,
                                            self->args_.interested_parties);
      self->result_->transport =
          grpc_create_chttp2_transport(args->args, args->endpoint, true);
      self->result_->socket_node =
          grpc_chttp2_transport_get_socket_node(self->result_->transport);
      GPR_ASSERT(self->result_->transport != nullptr);
      // TODO(roth): We ideally want to wait until we receive HTTP/2
      // settings from the server before we consider the connection
      // established.  If that doesn't happen before the connection
      // timeout expires, then we should consider the connection attempt a
      // failure and feed that information back into the backoff code.
      // We could pass a notify_on_receive_settings callback to
      // grpc_chttp2_transport_start_reading() to let us know when
      // settings are received, but we would need to figure out how to use
      // that information here.
      //
      // Unfortunately, we don't currently have a way to split apart the two
      // effects of scheduling c->notify: we start sending RPCs immediately
      // (which we want to do) and we consider the connection attempt successful
      // (which we don't want to do until we get the notify_on_receive_settings
      // callback from the transport).  If we could split those things
      // apart, then we could start sending RPCs but then wait for our
      // timeout before deciding if the connection attempt is successful.
      // If the attempt is not successful, then we would tear down the
      // transport and feed the failure back into the backoff code.
      //
      // In addition, even if we did that, we would probably not want to do
      // so until after transparent retries is implemented.  Otherwise, any
      // RPC that we attempt to send on the connection before the timeout
      // would fail instead of being retried on a subsequent attempt.
      grpc_chttp2_transport_start_reading(self->result_->transport,
                                          args->read_buffer, nullptr);
      self->result_->channel_args = args->args;
    } else {
      // If the handshaking succeeded but there is no endpoint, then the
      // handshaker may have handed off the connection to some external
      // code. Just verify that exit_early flag is set.
      GPR_DEBUG_ASSERT(args->exit_early);
    }
    grpc_closure* notify = self->notify_;
    self->notify_ = nullptr;
    ExecCtx::Run(DEBUG_LOCATION, notify, error);
    self->handshake_mgr_.reset();
  }
  self->Unref();
}

}  // namespace grpc_core
