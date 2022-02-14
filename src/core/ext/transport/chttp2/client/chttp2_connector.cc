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
#include <grpc/grpc_posix.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/connector.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/uri/uri_parser.h"

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD

#include <fcntl.h>

#include "src/core/lib/iomgr/tcp_client_posix.h"
#include "src/core/lib/iomgr/tcp_posix.h"

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD

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

namespace {

class Chttp2SecureClientChannelFactory : public ClientChannelFactory {
 public:
  RefCountedPtr<Subchannel> CreateSubchannel(
      const grpc_resolved_address& address,
      const grpc_channel_args* args) override {
    grpc_channel_args* new_args = GetSecureNamingChannelArgs(args);
    if (new_args == nullptr) {
      gpr_log(GPR_ERROR,
              "Failed to create channel args during subchannel creation.");
      return nullptr;
    }
    RefCountedPtr<Subchannel> s = Subchannel::Create(
        MakeOrphanable<Chttp2Connector>(), address, new_args);
    grpc_channel_args_destroy(new_args);
    return s;
  }

 private:
  static grpc_channel_args* GetSecureNamingChannelArgs(
      const grpc_channel_args* args) {
    grpc_channel_credentials* channel_credentials =
        grpc_channel_credentials_find_in_args(args);
    if (channel_credentials == nullptr) {
      gpr_log(GPR_ERROR,
              "Can't create subchannel: channel credentials missing for secure "
              "channel.");
      return nullptr;
    }
    // Make sure security connector does not already exist in args.
    if (grpc_security_connector_find_in_args(args) != nullptr) {
      gpr_log(GPR_ERROR,
              "Can't create subchannel: security connector already present in "
              "channel args.");
      return nullptr;
    }
    // Find the authority to use in the security connector.
    const char* authority =
        grpc_channel_args_find_string(args, GRPC_ARG_DEFAULT_AUTHORITY);
    GPR_ASSERT(authority != nullptr);
    // Create the security connector using the credentials and target name.
    grpc_channel_args* new_args_from_connector = nullptr;
    RefCountedPtr<grpc_channel_security_connector>
        subchannel_security_connector =
            channel_credentials->create_security_connector(
                /*call_creds=*/nullptr, authority, args,
                &new_args_from_connector);
    if (subchannel_security_connector == nullptr) {
      gpr_log(GPR_ERROR,
              "Failed to create secure subchannel for secure name '%s'",
              authority);
      return nullptr;
    }
    grpc_arg new_security_connector_arg =
        grpc_security_connector_to_arg(subchannel_security_connector.get());
    grpc_channel_args* new_args = grpc_channel_args_copy_and_add(
        new_args_from_connector != nullptr ? new_args_from_connector : args,
        &new_security_connector_arg, 1);
    subchannel_security_connector.reset(DEBUG_LOCATION, "lb_channel_create");
    grpc_channel_args_destroy(new_args_from_connector);
    return new_args;
  }
};

grpc_channel* CreateChannel(const char* target, const grpc_channel_args* args,
                            grpc_error_handle* error) {
  if (target == nullptr) {
    gpr_log(GPR_ERROR, "cannot create channel with NULL target name");
    if (error != nullptr) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("channel target is NULL");
    }
    return nullptr;
  }
  // Add channel arg containing the server URI.
  UniquePtr<char> canonical_target =
      ResolverRegistry::AddDefaultPrefixIfNeeded(target);
  grpc_arg arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_SERVER_URI), canonical_target.get());
  const char* to_remove[] = {GRPC_ARG_SERVER_URI};
  grpc_channel_args* new_args =
      grpc_channel_args_copy_and_add_and_remove(args, to_remove, 1, &arg, 1);
  grpc_channel* channel = grpc_channel_create_internal(
      target, new_args, GRPC_CLIENT_CHANNEL, nullptr, error);
  grpc_channel_args_destroy(new_args);
  return channel;
}

}  // namespace
}  // namespace grpc_core

namespace {

grpc_core::Chttp2SecureClientChannelFactory* g_factory;
gpr_once g_factory_once = GPR_ONCE_INIT;

void FactoryInit() {
  g_factory = new grpc_core::Chttp2SecureClientChannelFactory();
}

}  // namespace

// Create a secure client channel:
//   Asynchronously: - resolve target
//                   - connect to it (trying alternatives as presented)
//                   - perform handshakes
grpc_channel* grpc_channel_create(const char* target,
                                  grpc_channel_credentials* creds,
                                  const grpc_channel_args* args) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_secure_channel_create(target=%s, creds=%p, args=%p)", 3,
                 (target, (void*)creds, (void*)args));
  args = grpc_core::CoreConfiguration::Get()
             .channel_args_preconditioning()
             .PreconditionChannelArgs(args);
  grpc_channel* channel = nullptr;
  grpc_error_handle error = GRPC_ERROR_NONE;
  if (creds != nullptr) {
    // Add channel args containing the client channel factory and channel
    // credentials.
    gpr_once_init(&g_factory_once, FactoryInit);
    grpc_arg channel_factory_arg =
        grpc_core::ClientChannelFactory::CreateChannelArg(g_factory);
    grpc_arg args_to_add[] = {channel_factory_arg,
                              grpc_channel_credentials_to_arg(creds)};
    const char* arg_to_remove = channel_factory_arg.key;
    grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
        args, &arg_to_remove, 1, args_to_add, GPR_ARRAY_SIZE(args_to_add));
    new_args = creds->update_arguments(new_args);
    // Create channel.
    channel = grpc_core::CreateChannel(target, new_args, &error);
    // Clean up.
    grpc_channel_args_destroy(new_args);
  }
  grpc_channel_args_destroy(args);
  if (channel == nullptr) {
    intptr_t integer;
    grpc_status_code status = GRPC_STATUS_INTERNAL;
    if (grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &integer)) {
      status = static_cast<grpc_status_code>(integer);
    }
    GRPC_ERROR_UNREF(error);
    channel = grpc_lame_client_channel_create(
        target, status, "Failed to create secure client channel");
  }
  return channel;
}

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD
grpc_channel* grpc_channel_create_from_fd(const char* target, int fd,
                                          grpc_channel_credentials* creds,
                                          const grpc_channel_args* args) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE(
      "grpc_channel_create_from_fd(target=%p, fd=%d, creds=%p, args=%p)", 4,
      (target, fd, creds, args));
  // For now, we only support insecure channel credentials.
  if (creds == nullptr ||
      strcmp(creds->type(), GRPC_CREDENTIALS_TYPE_INSECURE) != 0) {
    return grpc_lame_client_channel_create(
        target, GRPC_STATUS_INTERNAL,
        "Failed to create client channel due to invalid creds");
  }
  grpc_arg default_authority_arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
      const_cast<char*>("test.authority"));
  args = grpc_channel_args_copy_and_add(args, &default_authority_arg, 1);
  const grpc_channel_args* final_args = grpc_core::CoreConfiguration::Get()
                                            .channel_args_preconditioning()
                                            .PreconditionChannelArgs(args);
  grpc_channel_args_destroy(args);

  int flags = fcntl(fd, F_GETFL, 0);
  GPR_ASSERT(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
  grpc_endpoint* client = grpc_tcp_client_create_from_fd(
      grpc_fd_create(fd, "client", true), final_args, "fd-client");
  grpc_transport* transport =
      grpc_create_chttp2_transport(final_args, client, true);
  GPR_ASSERT(transport);
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_channel* channel = grpc_channel_create_internal(
      target, final_args, GRPC_CLIENT_DIRECT_CHANNEL, transport, &error);
  grpc_channel_args_destroy(final_args);
  if (channel != nullptr) {
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
    grpc_core::ExecCtx::Get()->Flush();
  } else {
    intptr_t integer;
    grpc_status_code status = GRPC_STATUS_INTERNAL;
    if (grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &integer)) {
      status = static_cast<grpc_status_code>(integer);
    }
    GRPC_ERROR_UNREF(error);
    grpc_transport_destroy(transport);
    channel = grpc_lame_client_channel_create(
        target, status, "Failed to create client channel");
  }

  return channel;
}

#else  // !GPR_SUPPORT_CHANNELS_FROM_FD

grpc_channel* grpc_channel_create_from_fd(const char* /* target */,
                                          int /* fd */,
                                          grpc_channel_credentials* /* creds*/,
                                          const grpc_channel_args* /* args */) {
  GPR_ASSERT(0);
  return nullptr;
}

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD
