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
#include <initializer_list>
#include <string>
#include <utility>

#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call_trace.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

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
// MessageSizeFilter
//

const grpc_channel_filter ClientMessageSizeFilter::kFilter =
    MakePromiseBasedFilter<ClientMessageSizeFilter, FilterEndpoint::kClient,
                           kFilterExaminesOutboundMessages |
                               kFilterExaminesInboundMessages>("message_size");
const grpc_channel_filter ServerMessageSizeFilter::kFilter =
    MakePromiseBasedFilter<ServerMessageSizeFilter, FilterEndpoint::kServer,
                           kFilterExaminesOutboundMessages |
                               kFilterExaminesInboundMessages>("message_size");

class MessageSizeFilter::CallBuilder {
 private:
  auto Interceptor(uint32_t max_length, bool is_send) {
    return [max_length, is_send,
            err = err_](MessageHandle msg) -> absl::optional<MessageHandle> {
      if (grpc_call_trace.enabled()) {
        gpr_log(GPR_INFO, "%s[message_size] %s len:%" PRIdPTR " max:%d",
                Activity::current()->DebugTag().c_str(),
                is_send ? "send" : "recv", msg->payload()->Length(),
                max_length);
      }
      if (msg->payload()->Length() > max_length) {
        if (err->is_set()) return std::move(msg);
        auto r = GetContext<Arena>()->MakePooled<ServerMetadata>(
            GetContext<Arena>());
        r->Set(GrpcStatusMetadata(), GRPC_STATUS_RESOURCE_EXHAUSTED);
        r->Set(GrpcMessageMetadata(),
               Slice::FromCopiedString(
                   absl::StrFormat("%s message larger than max (%u vs. %d)",
                                   is_send ? "Sent" : "Received",
                                   msg->payload()->Length(), max_length)));
        err->Set(std::move(r));
        return absl::nullopt;
      }
      return std::move(msg);
    };
  }

 public:
  explicit CallBuilder(const MessageSizeParsedConfig& limits)
      : limits_(limits) {}

  template <typename T>
  void AddSend(T* pipe_end) {
    if (!limits_.max_send_size().has_value()) return;
    pipe_end->InterceptAndMap(Interceptor(*limits_.max_send_size(), true));
  }
  template <typename T>
  void AddRecv(T* pipe_end) {
    if (!limits_.max_recv_size().has_value()) return;
    pipe_end->InterceptAndMap(Interceptor(*limits_.max_recv_size(), false));
  }

  ArenaPromise<ServerMetadataHandle> Run(
      CallArgs call_args, NextPromiseFactory next_promise_factory) {
    return Race(err_->Wait(), next_promise_factory(std::move(call_args)));
  }

 private:
  Latch<ServerMetadataHandle>* const err_ =
      GetContext<Arena>()->ManagedNew<Latch<ServerMetadataHandle>>();
  MessageSizeParsedConfig limits_;
};

absl::StatusOr<ClientMessageSizeFilter> ClientMessageSizeFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  return ClientMessageSizeFilter(args);
}

absl::StatusOr<ServerMessageSizeFilter> ServerMessageSizeFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  return ServerMessageSizeFilter(args);
}

ArenaPromise<ServerMetadataHandle> ClientMessageSizeFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  // Get max sizes from channel data, then merge in per-method config values.
  // Note: Per-method config is only available on the client, so we
  // apply the max request size to the send limit and the max response
  // size to the receive limit.
  MessageSizeParsedConfig limits = this->limits();
  const MessageSizeParsedConfig* config_from_call_context =
      MessageSizeParsedConfig::GetFromCallContext(
          GetContext<grpc_call_context_element>(),
          service_config_parser_index_);
  if (config_from_call_context != nullptr) {
    absl::optional<uint32_t> max_send_size = limits.max_send_size();
    absl::optional<uint32_t> max_recv_size = limits.max_recv_size();
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
    limits = MessageSizeParsedConfig(max_send_size, max_recv_size);
  }

  CallBuilder b(limits);
  b.AddSend(call_args.client_to_server_messages);
  b.AddRecv(call_args.server_to_client_messages);
  return b.Run(std::move(call_args), std::move(next_promise_factory));
}

ArenaPromise<ServerMetadataHandle> ServerMessageSizeFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  CallBuilder b(limits());
  b.AddSend(call_args.server_to_client_messages);
  b.AddRecv(call_args.client_to_server_messages);
  return b.Run(std::move(call_args), std::move(next_promise_factory));
}

namespace {
// Used for GRPC_CLIENT_SUBCHANNEL
bool MaybeAddMessageSizeFilterToSubchannel(ChannelStackBuilder* builder) {
  if (builder->channel_args().WantMinimalStack()) {
    return true;
  }
  builder->PrependFilter(&ClientMessageSizeFilter::kFilter);
  return true;
}

// Used for GRPC_CLIENT_DIRECT_CHANNEL and GRPC_SERVER_CHANNEL. Adds the
// filter only if message size limits or service config is specified.
auto MaybeAddMessageSizeFilter(const grpc_channel_filter* filter) {
  return [filter](ChannelStackBuilder* builder) {
    auto channel_args = builder->channel_args();
    if (channel_args.WantMinimalStack()) {
      return true;
    }
    MessageSizeParsedConfig limits =
        MessageSizeParsedConfig::GetFromChannelArgs(channel_args);
    const bool enable =
        limits.max_send_size().has_value() ||
        limits.max_recv_size().has_value() ||
        channel_args.GetString(GRPC_ARG_SERVICE_CONFIG).has_value();
    if (enable) builder->PrependFilter(filter);
    return true;
  };
}

}  // namespace
void RegisterMessageSizeFilter(CoreConfiguration::Builder* builder) {
  MessageSizeParser::Register(builder);
  builder->channel_init()->RegisterStage(GRPC_CLIENT_SUBCHANNEL,
                                         GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                         MaybeAddMessageSizeFilterToSubchannel);
  builder->channel_init()->RegisterStage(
      GRPC_CLIENT_DIRECT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      MaybeAddMessageSizeFilter(&ClientMessageSizeFilter::kFilter));
  builder->channel_init()->RegisterStage(
      GRPC_SERVER_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      MaybeAddMessageSizeFilter(&ServerMessageSizeFilter::kFilter));
}
}  // namespace grpc_core
