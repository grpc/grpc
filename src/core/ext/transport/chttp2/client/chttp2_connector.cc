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

#include "src/core/ext/transport/chttp2/client/chttp2_connector.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "src/core/channelz/channelz.h"
#include "src/core/client_channel/client_channel_factory.h"
#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/client_channel/connector.h"
#include "src/core/client_channel/subchannel.h"
#include "src/core/config/core_configuration.h"
#include "src/core/credentials/transport/insecure/insecure_credentials.h"
#include "src/core/credentials/transport/security_connector.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#ifndef GRPC_EXPERIMENTAL_TEMPORARILY_DISABLE_PH2
// GRPC_EXPERIMENTAL_TEMPORARILY_DISABLE_PH2 is a temporary fix to help
// some customers who are having severe memory constraints. This macro
// will not always be available and we strongly recommend anyone to avoid
// the usage of this MACRO for any other purpose. We expect to delete this
// MACRO within 8-15 months.
#include "src/core/ext/transport/chttp2/transport/http2_client_transport.h"
#endif
#include "src/core/handshaker/handshaker.h"
#include "src/core/handshaker/handshaker_registry.h"
#include "src/core/handshaker/tcp_connect/tcp_connect_handshaker.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/endpoint_channel_arg_wrapper.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_create.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/resolver/resolver_registry.h"
#include "src/core/transport/endpoint_transport_client_channel_factory.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/time.h"
#include "src/core/util/unique_type_name.h"

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD

#include <fcntl.h>

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/tcp_client_posix.h"

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD

namespace grpc_core {

using ::grpc_event_engine::experimental::EventEngine;

namespace {
void NullThenSchedClosure(const DebugLocation& location, grpc_closure** closure,
                          grpc_error_handle error) {
  grpc_closure* c = *closure;
  *closure = nullptr;
  ExecCtx::Run(location, c, error);
}
}  // namespace

void Chttp2Connector::Connect(const Args& args, Result* result,
                              grpc_closure* notify) {
  {
    MutexLock lock(&mu_);
    GRPC_CHECK_EQ(notify_, nullptr);
    args_ = args;
    result_ = result;
    notify_ = notify;
    event_engine_ = args_.channel_args.GetObject<EventEngine>();
  }
  // Check if there is an endpoint in channel args.
  OrphanablePtr<grpc_endpoint> endpoint;
  auto endpoint_wrapper = args_.channel_args.GetObject<
      grpc_event_engine::experimental::EndpointChannelArgWrapper>();
  if (endpoint_wrapper != nullptr) {
    auto ee_endpoint = endpoint_wrapper->TakeEndpoint();
    if (ee_endpoint != nullptr) {
      endpoint.reset(grpc_event_engine_endpoint_create(std::move(ee_endpoint)));
    }
  }
  // If we weren't given the endpoint, add channel args needed by the
  // TCP connect handshaker.
  ChannelArgs channel_args = args_.channel_args;
  if (endpoint == nullptr) {
    absl::StatusOr<std::string> address = grpc_sockaddr_to_uri(args.address);
    if (!address.ok()) {
      grpc_error_handle error = GRPC_ERROR_CREATE(address.status().ToString());
      NullThenSchedClosure(DEBUG_LOCATION, &notify_, error);
      return;
    }
    channel_args =
        channel_args
            .Set(GRPC_ARG_TCP_HANDSHAKER_RESOLVED_ADDRESS, address.value())
            .Set(GRPC_ARG_TCP_HANDSHAKER_BIND_ENDPOINT_TO_POLLSET, 1);
  }
  handshake_mgr_ = MakeRefCounted<HandshakeManager>();
  CoreConfiguration::Get().handshaker_registry().AddHandshakers(
      HANDSHAKER_CLIENT, channel_args, args_.interested_parties,
      handshake_mgr_.get());
  handshake_mgr_->DoHandshake(std::move(endpoint), channel_args, args.deadline,
                              /*acceptor=*/nullptr,
                              [self = RefAsSubclass<Chttp2Connector>()](
                                  absl::StatusOr<HandshakerArgs*> result) {
                                self->OnHandshakeDone(std::move(result));
                              });
}

void Chttp2Connector::Shutdown(grpc_error_handle error) {
  MutexLock lock(&mu_);
  shutdown_ = true;
  if (handshake_mgr_ != nullptr) {
    // Handshaker will also shutdown the endpoint if it exists
    handshake_mgr_->Shutdown(error);
  }
}

void Chttp2Connector::OnHandshakeDone(absl::StatusOr<HandshakerArgs*> result) {
  MutexLock lock(&mu_);
  if (!result.ok() || shutdown_) {
    if (result.ok()) {
      result = GRPC_ERROR_CREATE("connector shutdown");
    }
    result_->Reset();
    NullThenSchedClosure(DEBUG_LOCATION, &notify_, result.status());
  } else if ((*result)->endpoint != nullptr) {
    const bool is_callv1 =
        !((*result)->args.GetBool(GRPC_ARG_USE_V3_STACK).value_or(false));
    if (is_callv1) {
      result_->transport = grpc_create_chttp2_transport(
          (*result)->args, std::move((*result)->endpoint), true);
      GRPC_CHECK_NE(result_->transport, nullptr);
      result_->channel_args = std::move((*result)->args);
      grpc_chttp2_transport_start_reading(
          result_->transport, (*result)->read_buffer.c_slice_buffer(),
          [self = RefAsSubclass<Chttp2Connector>()](
              absl::StatusOr<uint32_t> max_concurrent_streams) {
            self->OnReceiveSettings(max_concurrent_streams);
          },
          args_.interested_parties, nullptr);
      timer_handle_ = event_engine_->RunAfter(
          args_.deadline - Timestamp::Now(),
          [self = RefAsSubclass<Chttp2Connector>()]() mutable {
            ExecCtx exec_ctx;
            self->OnTimeout();
            // Ensure the Chttp2Connector is deleted under an ExecCtx.
            self.reset();
          });
#ifdef GRPC_EXPERIMENTAL_TEMPORARILY_DISABLE_PH2
      // GRPC_EXPERIMENTAL_TEMPORARILY_DISABLE_PH2 is a temporary fix to help
      // some customers who are having severe memory constraints. This macro
      // will not always be available and we strongly recommend anyone to avoid
      // the usage of this MACRO for any other purpose. We expect to delete this
      // MACRO within 8-15 months.
    }
#else
    } else {
      // TODO(tjagtap) : [PH2][P1] : Validate this code block thoroughly once
      // the ping pong test is in place.
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          event_engine_endpoint = grpc_event_engine::experimental::
              grpc_take_wrapped_event_engine_endpoint(
                  (*result)->endpoint.release());
      if (event_engine_endpoint == nullptr) {
        LOG(ERROR) << "Failed to take endpoint.";
        result = GRPC_ERROR_CREATE("Failed to take endpoint.");
      }
      // Create the PromiseEndpoint
      PromiseEndpoint promise_endpoint(std::move(event_engine_endpoint),
                                       std::move((*result)->read_buffer));
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine_ptr =
              (*result)
                  ->args
                  .GetObjectRef<grpc_event_engine::experimental::EventEngine>();
      // Http2ClientTransport does not take ownership of the channel args.
      result_->transport = new http2::Http2ClientTransport(
          std::move(promise_endpoint), (*result)->args, event_engine_ptr,
          [self = RefAsSubclass<Chttp2Connector>()](
              absl::StatusOr<uint32_t> max_concurrent_streams) {
            self->OnReceiveSettings(max_concurrent_streams);
          });
      result_->channel_args = std::move((*result)->args);
      GRPC_DCHECK_NE(result_->transport, nullptr);
      timer_handle_ = event_engine_->RunAfter(
          args_.deadline - Timestamp::Now(),
          [self = RefAsSubclass<Chttp2Connector>()]() mutable {
            ExecCtx exec_ctx;
            self->OnTimeout();
            // Ensure the Chttp2Connector is deleted under an ExecCtx.
            self.reset();
          });
    }
#endif  // GRPC_EXPERIMENTAL_TEMPORARILY_DISABLE_PH2
  } else {
    // If the handshaking succeeded but there is no endpoint, then the
    // handshaker may have handed off the connection to some external
    // code. Just verify that exit_early flag is set.
    GRPC_DCHECK((*result)->exit_early);
    NullThenSchedClosure(DEBUG_LOCATION, &notify_, result.status());
  }
  handshake_mgr_.reset();
}

void Chttp2Connector::OnReceiveSettings(
    absl::StatusOr<uint32_t> max_concurrent_streams) {
  MutexLock lock(&mu_);
  if (max_concurrent_streams.ok()) {
    result_->max_concurrent_streams = *max_concurrent_streams;
  }
  if (!notify_error_.has_value()) {
    if (!max_concurrent_streams.ok()) {
      // Transport got an error while waiting on SETTINGS frame.
      result_->Reset();
    }
    MaybeNotify(max_concurrent_streams.status());
    if (timer_handle_.has_value()) {
      if (event_engine_->Cancel(*timer_handle_)) {
        // If we have cancelled the timer successfully, call Notify() again
        // since the timer callback will not be called now.
        MaybeNotify(absl::OkStatus());
      }
      timer_handle_.reset();
    }
  } else {
    // OnTimeout() was already invoked. Call Notify() again so that notify_
    // can be invoked.
    MaybeNotify(absl::OkStatus());
  }
}

void Chttp2Connector::OnTimeout() {
  MutexLock lock(&mu_);
  timer_handle_.reset();
  if (!notify_error_.has_value()) {
    // The transport did not receive the settings frame in time. Destroy the
    // transport.
    result_->Reset();
    MaybeNotify(GRPC_ERROR_CREATE(
        "connection attempt timed out before receiving SETTINGS frame"));
  } else {
    // OnReceiveSettings() was already invoked. Call Notify() again so that
    // notify_ can be invoked.
    MaybeNotify(absl::OkStatus());
  }
}

void Chttp2Connector::MaybeNotify(grpc_error_handle error) {
  if (notify_error_.has_value()) {
    NullThenSchedClosure(DEBUG_LOCATION, &notify_, notify_error_.value());
    // Clear state for a new Connect().
    notify_error_.reset();
  } else {
    notify_error_ = error;
  }
}

absl::StatusOr<grpc_channel*> CreateHttp2Channel(std::string target,
                                                 const ChannelArgs& args) {
#ifdef GRPC_EXPERIMENTAL_TEMPORARILY_DISABLE_PH2
  // GRPC_EXPERIMENTAL_TEMPORARILY_DISABLE_PH2 is a temporary fix to help some
  // customers who are having severe memory constraints. This macro will not
  // always be available and we strongly recommend anyone to avoid the usage of
  // this MACRO for any other purpose. We expect to delete this MACRO within
  // 8-15 months.
  const bool is_v3 = false;
#else
  const bool is_v3 = IsPromiseBasedHttp2ClientTransportEnabled();
#endif  // GRPC_EXPERIMENTAL_TEMPORARILY_DISABLE_PH2
  auto r = ChannelCreate(
      target,
      args.SetObject(EndpointTransportClientChannelFactory<Chttp2Connector>())
          .Set(GRPC_ARG_USE_V3_STACK, is_v3),
      GRPC_CLIENT_CHANNEL, nullptr);
  if (r.ok()) {
    return r->release()->c_ptr();
  } else {
    return r.status();
  }
}

}  // namespace grpc_core

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD
grpc_channel* grpc_channel_create_from_fd(const char* target, int fd,
                                          grpc_channel_credentials* creds,
                                          const grpc_channel_args* args) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_channel_create_from_fd(target=" << target << ", fd=" << fd
      << ", creds=" << creds << ", args=" << args << ")";
  // For now, we only support insecure channel credentials.
  if (creds == nullptr ||
      creds->type() != grpc_core::InsecureCredentials::Type()) {
    return grpc_lame_client_channel_create(
        target, GRPC_STATUS_INTERNAL,
        "Failed to create client channel due to invalid creds");
  }
  grpc_core::ChannelArgs final_args =
      grpc_core::CoreConfiguration::Get()
          .channel_args_preconditioning()
          .PreconditionChannelArgs(args)
          .SetIfUnset(GRPC_ARG_DEFAULT_AUTHORITY, "test.authority")
          .SetObject(creds->Ref());

  int flags = fcntl(fd, F_GETFL, 0);
  GRPC_CHECK_EQ(fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0);
  grpc_core::OrphanablePtr<grpc_endpoint> client(grpc_tcp_create_from_fd(
      grpc_fd_create(fd, "client", true),
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(final_args),
      "fd-client"));
  grpc_core::Transport* transport =
      grpc_create_chttp2_transport(final_args, std::move(client), true);
  GRPC_CHECK(transport);
  auto channel = grpc_core::ChannelCreate(
      target, final_args, GRPC_CLIENT_DIRECT_CHANNEL, transport);
  if (channel.ok()) {
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr,
                                        nullptr);
    grpc_core::ExecCtx::Get()->Flush();
    return channel->release()->c_ptr();
  } else {
    transport->Orphan();
    return grpc_lame_client_channel_create(
        target, static_cast<grpc_status_code>(channel.status().code()),
        "Failed to create client channel");
  }
}

#else  // !GPR_SUPPORT_CHANNELS_FROM_FD

grpc_channel* grpc_channel_create_from_fd(const char* /* target */,
                                          int /* fd */,
                                          grpc_channel_credentials* /* creds*/,
                                          const grpc_channel_args* /* args */) {
  GRPC_CHECK(0);
  return nullptr;
}

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD
