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

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/connector.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

Chttp2Connector::Chttp2Connector() {
  GRPC_CLOSURE_INIT(&connected_, Connected, this, grpc_schedule_on_exec_ctx);
}

Chttp2Connector::~Chttp2Connector() {
  if (endpoint_ != nullptr) {
    grpc_endpoint_destroy(endpoint_);
  }
}

void Chttp2Connector::Connect(const Args& args, Result* result,
                              grpc_closure* notify) {
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
                          args.channel_args, args.address, args.deadline);
}

void Chttp2Connector::Shutdown(grpc_error_handle error) {
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

void Chttp2Connector::Connected(void* arg, grpc_error_handle error) {
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
  CoreConfiguration::Get().handshaker_registry().AddHandshakers(
      HANDSHAKER_CLIENT, args_.channel_args, args_.interested_parties,
      handshake_mgr_.get());
  grpc_endpoint_add_to_pollset_set(endpoint_, args_.interested_parties);
  handshake_mgr_->DoHandshake(endpoint_, args_.channel_args, args_.deadline,
                              nullptr /* acceptor */, OnHandshakeDone, this);
  endpoint_ = nullptr;  // Endpoint handed off to handshake manager.
}

namespace {
void NullThenSchedClosure(const DebugLocation& location, grpc_closure** closure,
                          grpc_error_handle error) {
  grpc_closure* c = *closure;
  *closure = nullptr;
  ExecCtx::Run(location, c, error);
}
}  // namespace

void Chttp2Connector::OnHandshakeDone(void* arg, grpc_error_handle error) {
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
      NullThenSchedClosure(DEBUG_LOCATION, &self->notify_, error);
    } else if (args->endpoint != nullptr) {
      self->result_->transport =
          grpc_create_chttp2_transport(args->args, args->endpoint, true);
      self->result_->socket_node =
          grpc_chttp2_transport_get_socket_node(self->result_->transport);
      self->result_->channel_args = args->args;
      GPR_ASSERT(self->result_->transport != nullptr);
      self->endpoint_ = args->endpoint;
      self->Ref().release();  // Ref held by OnReceiveSettings()
      GRPC_CLOSURE_INIT(&self->on_receive_settings_, OnReceiveSettings, self,
                        grpc_schedule_on_exec_ctx);
      self->Ref().release();  // Ref held by OnTimeout()
      grpc_chttp2_transport_start_reading(self->result_->transport,
                                          args->read_buffer,
                                          &self->on_receive_settings_, nullptr);
      GRPC_CLOSURE_INIT(&self->on_timeout_, OnTimeout, self,
                        grpc_schedule_on_exec_ctx);
      grpc_timer_init(&self->timer_, self->args_.deadline, &self->on_timeout_);
    } else {
      // If the handshaking succeeded but there is no endpoint, then the
      // handshaker may have handed off the connection to some external
      // code. Just verify that exit_early flag is set.
      GPR_DEBUG_ASSERT(args->exit_early);
      NullThenSchedClosure(DEBUG_LOCATION, &self->notify_, error);
    }
    self->handshake_mgr_.reset();
  }
  self->Unref();
}

void Chttp2Connector::OnReceiveSettings(void* arg, grpc_error_handle error) {
  Chttp2Connector* self = static_cast<Chttp2Connector*>(arg);
  {
    MutexLock lock(&self->mu_);
    if (!self->notify_error_.has_value()) {
      grpc_endpoint_delete_from_pollset_set(self->endpoint_,
                                            self->args_.interested_parties);
      if (error != GRPC_ERROR_NONE) {
        // Transport got an error while waiting on SETTINGS frame.
        // TODO(yashykt): The following two lines should be moved to
        // SubchannelConnector::Result::Reset()
        grpc_transport_destroy(self->result_->transport);
        grpc_channel_args_destroy(self->result_->channel_args);
        self->result_->Reset();
      }
      self->MaybeNotify(GRPC_ERROR_REF(error));
      grpc_timer_cancel(&self->timer_);
    } else {
      // OnTimeout() was already invoked. Call Notify() again so that notify_
      // can be invoked.
      self->MaybeNotify(GRPC_ERROR_NONE);
    }
  }
  self->Unref();
}

void Chttp2Connector::OnTimeout(void* arg, grpc_error_handle /*error*/) {
  Chttp2Connector* self = static_cast<Chttp2Connector*>(arg);
  {
    MutexLock lock(&self->mu_);
    if (!self->notify_error_.has_value()) {
      // The transport did not receive the settings frame in time. Destroy the
      // transport.
      grpc_endpoint_delete_from_pollset_set(self->endpoint_,
                                            self->args_.interested_parties);
      // TODO(yashykt): The following two lines should be moved to
      // SubchannelConnector::Result::Reset()
      grpc_transport_destroy(self->result_->transport);
      grpc_channel_args_destroy(self->result_->channel_args);
      self->result_->Reset();
      self->MaybeNotify(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "connection attempt timed out before receiving SETTINGS frame"));
    } else {
      // OnReceiveSettings() was already invoked. Call Notify() again so that
      // notify_ can be invoked.
      self->MaybeNotify(GRPC_ERROR_NONE);
    }
  }
  self->Unref();
}

void Chttp2Connector::MaybeNotify(grpc_error_handle error) {
  if (notify_error_.has_value()) {
    GRPC_ERROR_UNREF(error);
    NullThenSchedClosure(DEBUG_LOCATION, &notify_, notify_error_.value());
    // Clear state for a new Connect().
    // Clear out the endpoint_, since it is the responsibility of
    // the transport to shut it down.
    endpoint_ = nullptr;
    notify_error_.reset();
  } else {
    notify_error_ = error;
  }
}

}  // namespace grpc_core
