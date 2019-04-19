//
// Copyright 2019 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_MESSAGE_SIZE_MESSAGE_SIZE_PARSER_H
#define GRPC_CORE_EXT_FILTERS_MESSAGE_SIZE_MESSAGE_SIZE_PARSER_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/service_config.h"

namespace grpc_core {

class MessageSizeParsedObject : public ServiceConfigParsedObject {
 public:
  struct message_size_limits {
    int max_send_size;
    int max_recv_size;
  };

  MessageSizeParsedObject(int max_send_size, int max_recv_size) {
    limits_.max_send_size = max_send_size;
    limits_.max_recv_size = max_recv_size;
  }

  const message_size_limits& limits() const { return limits_; }

 private:
  message_size_limits limits_;
};

class MessageSizeParser : public ServiceConfigParser {
 public:
  UniquePtr<ServiceConfigParsedObject> ParsePerMethodParams(
      const grpc_json* json, grpc_error** error) override;

  static void Register();

  static size_t ParserIndex();
};
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_MESSAGE_SIZE_MESSAGE_SIZE_PARSER_H */
