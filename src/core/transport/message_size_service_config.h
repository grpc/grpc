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

#ifndef GRPC_SRC_CORE_TRANSPORT_MESSAGE_SIZE_SERVICE_CONFIG_H
#define GRPC_SRC_CORE_TRANSPORT_MESSAGE_SIZE_SERVICE_CONFIG_H

#include <grpc/support/port_platform.h>
#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>

#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/service_config/service_config_parser.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/validation_errors.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class MessageSizeParsedConfig : public ServiceConfigParser::ParsedConfig {
 public:
  std::optional<uint32_t> max_send_size() const { return max_send_size_; }
  std::optional<uint32_t> max_recv_size() const { return max_recv_size_; }

  MessageSizeParsedConfig() = default;

  MessageSizeParsedConfig(std::optional<uint32_t> max_send_size,
                          std::optional<uint32_t> max_recv_size)
      : max_send_size_(max_send_size), max_recv_size_(max_recv_size) {}

  static const MessageSizeParsedConfig* GetFromCallContext(
      Arena* arena, size_t service_config_parser_index);

  static MessageSizeParsedConfig GetFromChannelArgs(const ChannelArgs& args);

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);

 private:
  std::optional<uint32_t> max_send_size_;
  std::optional<uint32_t> max_recv_size_;
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

std::optional<uint32_t> GetMaxRecvSizeFromChannelArgs(const ChannelArgs& args);
std::optional<uint32_t> GetMaxSendSizeFromChannelArgs(const ChannelArgs& args);
std::optional<uint32_t> GetMaxRecvSizeFromCallContext(
    Arena* arena, std::optional<uint32_t> max_recv_size_from_channel_args);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_TRANSPORT_MESSAGE_SIZE_SERVICE_CONFIG_H
