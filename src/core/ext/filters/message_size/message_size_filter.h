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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_MESSAGE_SIZE_MESSAGE_SIZE_FILTER_H
#define GRPC_SRC_CORE_EXT_FILTERS_MESSAGE_SIZE_MESSAGE_SIZE_FILTER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/service_config/service_config_parser.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

class MessageSizeParsedConfig : public ServiceConfigParser::ParsedConfig {
 public:
  absl::optional<uint32_t> max_send_size() const { return max_send_size_; }
  absl::optional<uint32_t> max_recv_size() const { return max_recv_size_; }

  MessageSizeParsedConfig() = default;

  MessageSizeParsedConfig(absl::optional<uint32_t> max_send_size,
                          absl::optional<uint32_t> max_recv_size)
      : max_send_size_(max_send_size), max_recv_size_(max_recv_size) {}

  static const MessageSizeParsedConfig* GetFromCallContext(
      const grpc_call_context_element* context,
      size_t service_config_parser_index);

  static MessageSizeParsedConfig GetFromChannelArgs(const ChannelArgs& args);

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);

 private:
  absl::optional<uint32_t> max_send_size_;
  absl::optional<uint32_t> max_recv_size_;
};

class MessageSizeParser : public ServiceConfigParser::Parser {
 public:
  absl::string_view name() const override { return parser_name(); }

  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParsePerMethodParams(
      const ChannelArgs& /*args*/, const Json& json,
      ValidationErrors* errors) override;

  static void Register(CoreConfiguration::Builder* builder);

  static size_t ParserIndex();

 private:
  static absl::string_view parser_name() { return "message_size"; }
};

absl::optional<uint32_t> GetMaxRecvSizeFromChannelArgs(const ChannelArgs& args);
absl::optional<uint32_t> GetMaxSendSizeFromChannelArgs(const ChannelArgs& args);

class ServerMessageSizeFilter final
    : public ImplementChannelFilter<ServerMessageSizeFilter> {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<ServerMessageSizeFilter> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  class Call {
   public:
    static const NoInterceptor OnClientInitialMetadata;
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnFinalize;
    ServerMetadataHandle OnClientToServerMessage(
        const Message& message, ServerMessageSizeFilter* filter);
    ServerMetadataHandle OnServerToClientMessage(
        const Message& message, ServerMessageSizeFilter* filter);
  };

 private:
  explicit ServerMessageSizeFilter(const ChannelArgs& args)
      : parsed_config_(MessageSizeParsedConfig::GetFromChannelArgs(args)) {}
  const MessageSizeParsedConfig parsed_config_;
};

class ClientMessageSizeFilter final
    : public ImplementChannelFilter<ClientMessageSizeFilter> {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<ClientMessageSizeFilter> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  class Call {
   public:
    explicit Call(ClientMessageSizeFilter* filter);

    static const NoInterceptor OnClientInitialMetadata;
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnFinalize;
    ServerMetadataHandle OnClientToServerMessage(const Message& message);
    ServerMetadataHandle OnServerToClientMessage(const Message& message);

   private:
    MessageSizeParsedConfig limits_;
  };

 private:
  explicit ClientMessageSizeFilter(const ChannelArgs& args)
      : parsed_config_(MessageSizeParsedConfig::GetFromChannelArgs(args)) {}
  const size_t service_config_parser_index_{MessageSizeParser::ParserIndex()};
  const MessageSizeParsedConfig parsed_config_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_MESSAGE_SIZE_MESSAGE_SIZE_FILTER_H
