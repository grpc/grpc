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

#ifndef GRPC_CORE_EXT_FILTERS_MESSAGE_SIZE_MESSAGE_SIZE_FILTER_H
#define GRPC_CORE_EXT_FILTERS_MESSAGE_SIZE_MESSAGE_SIZE_FILTER_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/service_config_call_data.h"
#include "src/core/ext/filters/client_channel/service_config_parser.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"

extern const grpc_channel_filter grpc_message_size_filter;

namespace grpc_core {

class MessageSizeParsedConfig : public ServiceConfigParser::ParsedConfig {
 public:
  struct message_size_limits {
    int max_send_size;
    int max_recv_size;
  };

  MessageSizeParsedConfig(int max_send_size, int max_recv_size) {
    limits_.max_send_size = max_send_size;
    limits_.max_recv_size = max_recv_size;
  }

  const message_size_limits& limits() const { return limits_; }

 private:
  message_size_limits limits_;
};

class MessageSizeParser : public ServiceConfigParser::Parser {
 public:
  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParsePerMethodParams(
      const Json& json, grpc_error** error) override;

  static void Register();

  static size_t ParserIndex();
};

int get_max_recv_size(const grpc_channel_args* args) {
  return grpc_channel_args_find_integer(
      args, GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH,
      {grpc_channel_args_want_minimal_stack(args)
           ? -1
           : GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH,
       -1, INT_MAX});
}

int get_max_send_size(const grpc_channel_args* args) {
  return grpc_channel_args_find_integer(
      args, GRPC_ARG_MAX_SEND_MESSAGE_LENGTH,
      {grpc_channel_args_want_minimal_stack(args)
           ? -1
           : GRPC_DEFAULT_MAX_SEND_MESSAGE_LENGTH,
       -1, INT_MAX});
}

const MessageSizeParsedConfig* get_message_size_config_from_call_context(
    const grpc_call_context_element* context) {
  grpc_core::ServiceConfigCallData* svc_cfg_call_data = nullptr;
  if (context != nullptr) {
    svc_cfg_call_data = static_cast<grpc_core::ServiceConfigCallData*>(
        context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
  }
  if (svc_cfg_call_data != nullptr) {
    return static_cast<const grpc_core::MessageSizeParsedConfig*>(
        svc_cfg_call_data->GetMethodParsedConfig(
            grpc_core::MessageSizeParser::ParserIndex()));
  }
  return nullptr;
}

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_MESSAGE_SIZE_MESSAGE_SIZE_FILTER_H */
