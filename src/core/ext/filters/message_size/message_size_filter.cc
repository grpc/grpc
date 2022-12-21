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

#include <new>

#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/promise/map_pipe.h"
#include "src/core/lib/promise/try_concurrently.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

const grpc_channel_filter ClientMessageSizeFilter::kFilter =
    MakePromiseBasedFilter<ClientMessageSizeFilter, FilterEndpoint::kClient,
                           kFilterExaminesInboundMessages |
                               kFilterExaminesOutboundMessages>("message_size");
const grpc_channel_filter ServerMessageSizeFilter::kFilter =
    MakePromiseBasedFilter<ServerMessageSizeFilter, FilterEndpoint::kServer,
                           kFilterExaminesInboundMessages |
                               kFilterExaminesOutboundMessages>("message_size");

//
// MessageSizeParsedConfig
//

const MessageSizeParsedConfig* MessageSizeParsedConfig::GetFromCallContext(
    const grpc_call_context_element* context,
    size_t service_config_parser_index) {
  if (context == nullptr) return nullptr;
  auto* svc_cfg_call_data = static_cast<ServiceConfigCallData*>(
      context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
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
// ClientMessageSizeFilter
//

absl::StatusOr<ClientMessageSizeFilter> ClientMessageSizeFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args cfargs) {
  return ClientMessageSizeFilter(args);
}

ClientMessageSizeFilter::ClientMessageSizeFilter(const ChannelArgs& args)
    : filter_state_{MessageSizeParser::ParserIndex(),
                    MessageSizeParsedConfig::GetFromChannelArgs(args)} {}

void MaybeUpdateLimitsFromCallContext(MessageSizeFilterState& filter_state,
                                      const MessageSizeParsedConfig* config) {
  // maybe override channel limits with call limits
  if (config != nullptr) {
    absl::optional<uint32_t> max_send_size =
        filter_state.limits.max_send_size();
    absl::optional<uint32_t> max_recv_size =
        filter_state.limits.max_recv_size();
    if (config->max_send_size().has_value() &&
        (!max_send_size.has_value() ||
         *config->max_send_size() < *max_send_size)) {
      max_send_size = *config->max_send_size();
    }
    if (config->max_recv_size().has_value() &&
        (!max_recv_size.has_value() ||
         *config->max_recv_size() < *max_recv_size)) {
      max_recv_size = *config->max_recv_size();
    }
    filter_state.limits = MessageSizeParsedConfig(max_send_size, max_recv_size);
  }
}

ArenaPromise<ServerMetadataHandle> ClientMessageSizeFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  auto incoming_pipe =
      PipeMapper<MessageHandle>::Intercept(*call_args.incoming_messages);
  auto outgoing_pipe =
      PipeMapper<MessageHandle>::Intercept(*call_args.outgoing_messages);

  MaybeUpdateLimitsFromCallContext(
      filter_state_, MessageSizeParsedConfig::GetFromCallContext(
                         GetContext<grpc_call_context_element>(),
                         filter_state_.service_config_parser_index));

  return TryConcurrently(next_promise_factory(std::move(call_args)))
      .NecessaryPush(outgoing_pipe.TakeAndRun(
          [&limits = filter_state_.limits](
              MessageHandle handle) -> absl::StatusOr<MessageHandle> {
            if (limits.max_send_size().has_value() &&
                limits.max_send_size().value() < handle->payload()->Length()) {
              return absl::ResourceExhaustedError(absl::StrFormat(
                  "Sent message larger than max (%u vs. %d)",
                  handle->payload()->Length(), limits.max_send_size().value()));
            }
            return handle;
          }))
      .Pull(incoming_pipe.TakeAndRun(
          [&limits = filter_state_.limits](
              MessageHandle handle) -> absl::StatusOr<MessageHandle> {
            if (limits.max_recv_size().has_value() &&
                limits.max_recv_size().value() < handle->payload()->Length()) {
              return absl::ResourceExhaustedError(absl::StrFormat(
                  "Received message larger than max (%u vs. %d)",
                  handle->payload()->Length(), limits.max_send_size().value()));
            }
            return handle;
          }));
}

//
// ServerMessageSizeFilter
//

absl::StatusOr<ServerMessageSizeFilter> ServerMessageSizeFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  return ServerMessageSizeFilter(args);
}

ServerMessageSizeFilter::ServerMessageSizeFilter(const ChannelArgs& args)
    : filter_state_{MessageSizeParser::ParserIndex(),
                    MessageSizeParsedConfig::GetFromChannelArgs(args)} {}

ArenaPromise<ServerMetadataHandle> ServerMessageSizeFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  auto incoming_pipe =
      PipeMapper<MessageHandle>::Intercept(*call_args.incoming_messages);
  auto outgoing_pipe =
      PipeMapper<MessageHandle>::Intercept(*call_args.outgoing_messages);

  MaybeUpdateLimitsFromCallContext(
      filter_state_, MessageSizeParsedConfig::GetFromCallContext(
                         GetContext<grpc_call_context_element>(),
                         filter_state_.service_config_parser_index));

  return TryConcurrently(next_promise_factory(std::move(call_args)))
      .Push(outgoing_pipe.TakeAndRun(
          [&limits = filter_state_.limits](
              MessageHandle handle) -> absl::StatusOr<MessageHandle> {
            if (limits.max_send_size().has_value() &&
                limits.max_send_size().value() < handle->payload()->Length()) {
              return absl::ResourceExhaustedError(absl::StrFormat(
                  "Sent message larger than max (%u vs. %d)",
                  handle->payload()->Length(), limits.max_send_size().value()));
            }
            return handle;
          }))
      .NecessaryPull(incoming_pipe.TakeAndRun(
          [&limits = filter_state_.limits](
              MessageHandle handle) -> absl::StatusOr<MessageHandle> {
            if (limits.max_recv_size().has_value() &&
                limits.max_recv_size().value() < handle->payload()->Length()) {
              return absl::ResourceExhaustedError(absl::StrFormat(
                  "Received message larger than max (%u vs. %d)",
                  handle->payload()->Length(), limits.max_send_size().value()));
            }
            return handle;
          }));
}

namespace {
bool maybe_add_message_size_filter(ChannelStackBuilder* builder,
                                   const grpc_channel_filter* filter,
                                   bool check_limits) {
  auto channel_args = builder->channel_args();
  if (channel_args.WantMinimalStack()) {
    return true;
  }
  if (!check_limits) {
    builder->PrependFilter(filter);
    return true;
  }
  MessageSizeParsedConfig limits =
      MessageSizeParsedConfig::GetFromChannelArgs(channel_args);
  const bool enable =
      limits.max_send_size().has_value() ||
      limits.max_recv_size().has_value() ||
      channel_args.GetString(GRPC_ARG_SERVICE_CONFIG).has_value();
  if (enable) {
    builder->PrependFilter(filter);
  }
  return true;
}

bool maybe_add_message_size_filter_client_direct(ChannelStackBuilder* builder) {
  return maybe_add_message_size_filter(builder,
                                       &ClientMessageSizeFilter::kFilter,
                                       /*check_limits=*/true);
}

bool maybe_add_message_size_filter_subchannel(ChannelStackBuilder* builder) {
  return maybe_add_message_size_filter(builder,
                                       &ClientMessageSizeFilter::kFilter,
                                       /*check_limits=*/false);
}

bool maybe_add_message_size_filter_server(ChannelStackBuilder* builder) {
  return maybe_add_message_size_filter(builder,
                                       &ServerMessageSizeFilter::kFilter,
                                       /*check_limits=*/true);
}
}  // namespace

void RegisterMessageSizeFilter(CoreConfiguration::Builder* builder) {
  MessageSizeParser::Register(builder);
  builder->channel_init()->RegisterStage(
      GRPC_CLIENT_SUBCHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      maybe_add_message_size_filter_subchannel);
  builder->channel_init()->RegisterStage(
      GRPC_CLIENT_DIRECT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      maybe_add_message_size_filter_client_direct);
  builder->channel_init()->RegisterStage(GRPC_SERVER_CHANNEL,
                                         GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                         maybe_add_message_size_filter_server);
}
}  // namespace grpc_core
