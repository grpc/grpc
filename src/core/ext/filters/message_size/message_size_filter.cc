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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/message_size/message_size_filter.h"

#include <inttypes.h>

#include <functional>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/service_config/service_config_call_data.h"

namespace grpc_core {

const NoInterceptor ClientMessageSizeFilter::Call::OnClientInitialMetadata;
const NoInterceptor ClientMessageSizeFilter::Call::OnServerInitialMetadata;
const NoInterceptor ClientMessageSizeFilter::Call::OnServerTrailingMetadata;
const NoInterceptor ClientMessageSizeFilter::Call::OnClientToServerHalfClose;
const NoInterceptor ClientMessageSizeFilter::Call::OnFinalize;
const NoInterceptor ServerMessageSizeFilter::Call::OnClientInitialMetadata;
const NoInterceptor ServerMessageSizeFilter::Call::OnServerInitialMetadata;
const NoInterceptor ServerMessageSizeFilter::Call::OnServerTrailingMetadata;
const NoInterceptor ServerMessageSizeFilter::Call::OnClientToServerHalfClose;
const NoInterceptor ServerMessageSizeFilter::Call::OnFinalize;

//
// MessageSizeParsedConfig
//

const MessageSizeParsedConfig* MessageSizeParsedConfig::GetFromCallContext(
    Arena* arena, size_t service_config_parser_index) {
  auto* svc_cfg_call_data = arena->GetContext<ServiceConfigCallData>();
  if (svc_cfg_call_data == nullptr) return nullptr;
  return static_cast<const MessageSizeParsedConfig*>(
      svc_cfg_call_data->GetMethodParsedConfig(service_config_parser_index));
}

MessageSizeParsedConfig MessageSizeParsedConfig::GetFromChannelArgs(
    const ChannelArgs& channel_args) {
  MessageSizeParsedConfig limits;
  limits.max_send_size_ = GetMaxSendSizeFromChannelArgs(channel_args);
  limits.max_recv_size_ = GetMaxRecvSizeFromChannelArgs(channel_args);
  return limits;
}

absl::optional<uint32_t> GetMaxRecvSizeFromChannelArgs(
    const ChannelArgs& args) {
  if (args.WantMinimalStack()) return absl::nullopt;
  int size = args.GetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH)
                 .value_or(GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH);
  if (size < 0) return absl::nullopt;
  return static_cast<uint32_t>(size);
}

absl::optional<uint32_t> GetMaxSendSizeFromChannelArgs(
    const ChannelArgs& args) {
  if (args.WantMinimalStack()) return absl::nullopt;
  int size = args.GetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH)
                 .value_or(GRPC_DEFAULT_MAX_SEND_MESSAGE_LENGTH);
  if (size < 0) return absl::nullopt;
  return static_cast<uint32_t>(size);
}

const JsonLoaderInterface* MessageSizeParsedConfig::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<MessageSizeParsedConfig>()
          .OptionalField("maxRequestMessageBytes",
                         &MessageSizeParsedConfig::max_send_size_)
          .OptionalField("maxResponseMessageBytes",
                         &MessageSizeParsedConfig::max_recv_size_)
          .Finish();
  return loader;
}

//
// MessageSizeParser
//

std::unique_ptr<ServiceConfigParser::ParsedConfig>
MessageSizeParser::ParsePerMethodParams(const ChannelArgs& /*args*/,
                                        const Json& json,
                                        ValidationErrors* errors) {
  return LoadFromJson<std::unique_ptr<MessageSizeParsedConfig>>(
      json, JsonArgs(), errors);
}

void MessageSizeParser::Register(CoreConfiguration::Builder* builder) {
  builder->service_config_parser()->RegisterParser(
      std::make_unique<MessageSizeParser>());
}

size_t MessageSizeParser::ParserIndex() {
  return CoreConfiguration::Get().service_config_parser().GetParserIndex(
      parser_name());
}

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
                                  absl::optional<uint32_t> max_length,
                                  bool is_client, bool is_send) {
  if (!max_length.has_value()) return nullptr;
  if (GRPC_TRACE_FLAG_ENABLED(call)) {
    LOG(INFO) << GetContext<Activity>()->DebugTag() << "[message_size] "
              << (is_send ? "send" : "recv")
              << " len:" << msg.payload()->Length() << " max:" << *max_length;
  }
  if (msg.payload()->Length() <= *max_length) return nullptr;
  return ServerMetadataFromStatus(
      GRPC_STATUS_RESOURCE_EXHAUSTED,
      absl::StrFormat("%s: %s message larger than max (%u vs. %d)",
                      is_client ? "CLIENT" : "SERVER",
                      is_send ? "Sent" : "Received", msg.payload()->Length(),
                      *max_length));
}
}  // namespace

ClientMessageSizeFilter::Call::Call(ClientMessageSizeFilter* filter)
    : limits_(filter->parsed_config_) {
  // Get max sizes from channel data, then merge in per-method config values.
  // Note: Per-method config is only available on the client, so we
  // apply the max request size to the send limit and the max response
  // size to the receive limit.
  const MessageSizeParsedConfig* config_from_call_context =
      MessageSizeParsedConfig::GetFromCallContext(
          GetContext<Arena>(), filter->service_config_parser_index_);
  if (config_from_call_context != nullptr) {
    absl::optional<uint32_t> max_send_size = limits_.max_send_size();
    absl::optional<uint32_t> max_recv_size = limits_.max_recv_size();
    if (config_from_call_context->max_send_size().has_value() &&
        (!max_send_size.has_value() ||
         *config_from_call_context->max_send_size() < *max_send_size)) {
      max_send_size = *config_from_call_context->max_send_size();
    }
    if (config_from_call_context->max_recv_size().has_value() &&
        (!max_recv_size.has_value() ||
         *config_from_call_context->max_recv_size() < *max_recv_size)) {
      max_recv_size = *config_from_call_context->max_recv_size();
    }
    limits_ = MessageSizeParsedConfig(max_send_size, max_recv_size);
  }
}

ServerMetadataHandle ServerMessageSizeFilter::Call::OnClientToServerMessage(
    const Message& message, ServerMessageSizeFilter* filter) {
  return CheckPayload(message, filter->parsed_config_.max_recv_size(),
                      /*is_client=*/false, false);
}

ServerMetadataHandle ServerMessageSizeFilter::Call::OnServerToClientMessage(
    const Message& message, ServerMessageSizeFilter* filter) {
  return CheckPayload(message, filter->parsed_config_.max_send_size(),
                      /*is_client=*/false, true);
}

ServerMetadataHandle ClientMessageSizeFilter::Call::OnClientToServerMessage(
    const Message& message) {
  return CheckPayload(message, limits_.max_send_size(), /*is_client=*/true,
                      true);
}

ServerMetadataHandle ClientMessageSizeFilter::Call::OnServerToClientMessage(
    const Message& message) {
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
