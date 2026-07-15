//
// Copyright 2026 gRPC authors.
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

#include "src/core/transport/message_size_service_config.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>

#include <cstddef>
#include <cstdint>
#include <optional>

#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/service_config/service_config_call_data.h"

namespace grpc_core {

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

std::optional<uint32_t> GetMaxRecvSizeFromChannelArgs(const ChannelArgs& args) {
  if (args.WantMinimalStack()) return std::nullopt;
  int size = args.GetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH)
                 .value_or(GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH);
  if (size < 0) return std::nullopt;
  return static_cast<uint32_t>(size);
}

std::optional<uint32_t> GetMaxSendSizeFromChannelArgs(const ChannelArgs& args) {
  if (args.WantMinimalStack()) return std::nullopt;
  int size = args.GetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH)
                 .value_or(GRPC_DEFAULT_MAX_SEND_MESSAGE_LENGTH);
  if (size < 0) return std::nullopt;
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

std::optional<uint32_t> GetMaxRecvSizeFromCallContext(
    Arena* arena, std::optional<uint32_t> max_recv_size_from_channel_args) {
  const MessageSizeParsedConfig* config_from_call_context =
      MessageSizeParsedConfig::GetFromCallContext(
          arena, MessageSizeParser::ParserIndex());
  if (config_from_call_context == nullptr) {
    return max_recv_size_from_channel_args;
  }

  if (config_from_call_context->max_recv_size().has_value() &&
      (!max_recv_size_from_channel_args.has_value() ||
       *config_from_call_context->max_recv_size() <
           *max_recv_size_from_channel_args)) {
    return config_from_call_context->max_recv_size();
  }
  return max_recv_size_from_channel_args;
}

}  // namespace grpc_core
