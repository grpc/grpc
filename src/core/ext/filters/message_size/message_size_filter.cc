//
// Copyright 2016 gRPC authors.
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

#include "src/core/ext/filters/message_size/message_size_filter.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>

#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/transport/message_size_service_config.h"
#include "src/core/util/latent_see.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"

namespace grpc_core {

//
// MessageSizeFilter
//

const grpc_channel_filter ClientMessageSizeFilter::kFilter =
    MakePromiseBasedFilter<ClientMessageSizeFilter, FilterEndpoint::kClient,
                           kFilterExaminesOutboundMessages |
                               kFilterExaminesInboundMessages>();

const grpc_channel_filter ServerMessageSizeFilter::kFilter =
    MakePromiseBasedFilter<ServerMessageSizeFilter, FilterEndpoint::kServer,
                           kFilterExaminesOutboundMessages |
                               kFilterExaminesInboundMessages>();

absl::StatusOr<std::unique_ptr<ClientMessageSizeFilter>>
ClientMessageSizeFilter::Create(const ChannelArgs& args, ChannelFilter::Args) {
  return std::make_unique<ClientMessageSizeFilter>(args);
}

absl::StatusOr<std::unique_ptr<ServerMessageSizeFilter>>
ServerMessageSizeFilter::Create(const ChannelArgs& args, ChannelFilter::Args) {
  return std::make_unique<ServerMessageSizeFilter>(args);
}

namespace {
ServerMetadataHandle CheckPayload(const Message& msg,
                                  std::optional<uint32_t> max_length,
                                  bool is_client, bool is_send) {
  if (!max_length.has_value()) return nullptr;
  GRPC_TRACE_LOG(call, INFO)
      << GetContext<Activity>()->DebugTag() << "[message_size] "
      << (is_send ? "send" : "recv") << " len:" << msg.payload()->Length()
      << " max:" << *max_length;
  if (msg.payload()->Length() <= *max_length) return nullptr;
  return CancelledServerMetadataFromStatus(
      GRPC_STATUS_RESOURCE_EXHAUSTED,
      absl::StrFormat("%s: %s message larger than max (%u vs. %d)",
                      is_client ? "CLIENT" : "SERVER",
                      is_send ? "Sent" : "Received", msg.payload()->Length(),
                      *max_length));
}
}  // namespace

void ClientMessageSizeFilter::Call::OnClientInitialMetadata(
    ClientMetadata&, ClientMessageSizeFilter* filter) {
  limits_ = filter->parsed_config_;
  // Get max sizes from channel data, then merge in per-method config values.
  // Note: Per-method config is only available on the client, so we
  // apply the max request size to the send limit and the max response
  // size to the receive limit.
  const MessageSizeParsedConfig* config_from_call_context =
      MessageSizeParsedConfig::GetFromCallContext(
          GetContext<Arena>(), filter->service_config_parser_index_);
  if (config_from_call_context != nullptr) {
    std::optional<uint32_t> max_send_size = limits_.max_send_size();
    std::optional<uint32_t> max_recv_size = limits_.max_recv_size();
    if (config_from_call_context->max_send_size().has_value() &&
        (!max_send_size.has_value() ||
         *config_from_call_context->max_send_size() < *max_send_size)) {
      max_send_size = config_from_call_context->max_send_size();
    }
    if (config_from_call_context->max_recv_size().has_value() &&
        (!max_recv_size.has_value() ||
         *config_from_call_context->max_recv_size() < *max_recv_size)) {
      max_recv_size = config_from_call_context->max_recv_size();
    }
    limits_ = MessageSizeParsedConfig(max_send_size, max_recv_size);
  }
}

ServerMetadataHandle ServerMessageSizeFilter::Call::OnClientToServerMessage(
    const Message& message, ServerMessageSizeFilter* filter) {
  GRPC_LATENT_SEE_SCOPE(
      "ServerMessageSizeFilter::Call::OnClientToServerMessage");
  return CheckPayload(message, filter->parsed_config_.max_recv_size(),
                      /*is_client=*/false, false);
}

ServerMetadataHandle ServerMessageSizeFilter::Call::OnServerToClientMessage(
    const Message& message, ServerMessageSizeFilter* filter) {
  GRPC_LATENT_SEE_SCOPE(
      "ServerMessageSizeFilter::Call::OnServerToClientMessage");
  return CheckPayload(message, filter->parsed_config_.max_send_size(),
                      /*is_client=*/false, true);
}

ServerMetadataHandle ClientMessageSizeFilter::Call::OnClientToServerMessage(
    const Message& message) {
  GRPC_LATENT_SEE_SCOPE(
      "ClientMessageSizeFilter::Call::OnClientToServerMessage");
  return CheckPayload(message, limits_.max_send_size(), /*is_client=*/true,
                      true);
}

ServerMetadataHandle ClientMessageSizeFilter::Call::OnServerToClientMessage(
    const Message& message) {
  GRPC_LATENT_SEE_SCOPE(
      "ClientMessageSizeFilter::Call::OnServerToClientMessage");
  return CheckPayload(message, limits_.max_recv_size(), /*is_client=*/true,
                      false);
}

namespace {
// Used for GRPC_CLIENT_DIRECT_CHANNEL and GRPC_SERVER_CHANNEL. Adds the
// filter only if message size limits or service config is specified.
bool HasMessageSizeLimits(const ChannelArgs& channel_args) {
  MessageSizeParsedConfig limits =
      MessageSizeParsedConfig::GetFromChannelArgs(channel_args);
  return limits.max_send_size().has_value() ||
         limits.max_recv_size().has_value() ||
         channel_args.GetString(GRPC_ARG_SERVICE_CONFIG).has_value();
}

}  // namespace
void RegisterMessageSizeFilter(CoreConfiguration::Builder* builder) {
  MessageSizeParser::Register(builder);
  builder->channel_init()
      ->RegisterFilter<ClientMessageSizeFilter>(GRPC_CLIENT_SUBCHANNEL)
      .ExcludeFromMinimalStack();
  builder->channel_init()
      ->RegisterFilter<ClientMessageSizeFilter>(GRPC_CLIENT_DIRECT_CHANNEL)
      .ExcludeFromMinimalStack()
      .If(HasMessageSizeLimits);
  builder->channel_init()
      ->RegisterFilter<ServerMessageSizeFilter>(GRPC_SERVER_CHANNEL)
      .ExcludeFromMinimalStack()
      .If(HasMessageSizeLimits);
}
}  // namespace grpc_core
