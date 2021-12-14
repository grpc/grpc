/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include <string.h>

#include "src/core/ext/filters/http/client/http_client_filter.h"
#include "src/core/ext/filters/http/message_compress/message_compress_filter.h"
#include "src/core/ext/filters/http/message_compress/message_decompress_filter.h"
#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/transport_impl.h"

static bool is_building_http_like_transport(
    grpc_channel_stack_builder* builder) {
  grpc_transport* t = grpc_channel_stack_builder_get_transport(builder);
  return t != nullptr && strstr(t->vtable->name, "http");
}

namespace grpc_core {
void RegisterHttpFilters(CoreConfiguration::Builder* builder) {
  auto optional = [builder](grpc_channel_stack_type channel_type,
                            bool enable_in_minimal_stack,
                            const char* control_channel_arg,
                            const grpc_channel_filter* filter) {
    builder->channel_init()->RegisterStage(
        channel_type, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
        [enable_in_minimal_stack, control_channel_arg,
         filter](grpc_channel_stack_builder* builder) {
          if (!is_building_http_like_transport(builder)) return true;
          const grpc_channel_args* channel_args =
              grpc_channel_stack_builder_get_channel_arguments(builder);
          bool enable = grpc_channel_arg_get_bool(
              grpc_channel_args_find(channel_args, control_channel_arg),
              enable_in_minimal_stack ||
                  !grpc_channel_args_want_minimal_stack(channel_args));
          if (!enable) return true;
          return grpc_channel_stack_builder_prepend_filter(builder, filter,
                                                           nullptr, nullptr);
        });
  };
  auto required = [builder](grpc_channel_stack_type channel_type,
                            const grpc_channel_filter* filter) {
    builder->channel_init()->RegisterStage(
        channel_type, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
        [filter](grpc_channel_stack_builder* builder) {
          if (!is_building_http_like_transport(builder)) return true;
          return grpc_channel_stack_builder_prepend_filter(builder, filter,
                                                           nullptr, nullptr);
        });
  };
  optional(GRPC_CLIENT_SUBCHANNEL, false,
           GRPC_ARG_ENABLE_PER_MESSAGE_COMPRESSION,
           &grpc_message_compress_filter);
  optional(GRPC_CLIENT_DIRECT_CHANNEL, false,
           GRPC_ARG_ENABLE_PER_MESSAGE_COMPRESSION,
           &grpc_message_compress_filter);
  optional(GRPC_SERVER_CHANNEL, false, GRPC_ARG_ENABLE_PER_MESSAGE_COMPRESSION,
           &grpc_message_compress_filter);
  optional(GRPC_CLIENT_SUBCHANNEL, true,
           GRPC_ARG_ENABLE_PER_MESSAGE_DECOMPRESSION, &MessageDecompressFilter);
  optional(GRPC_CLIENT_DIRECT_CHANNEL, true,
           GRPC_ARG_ENABLE_PER_MESSAGE_DECOMPRESSION, &MessageDecompressFilter);
  optional(GRPC_SERVER_CHANNEL, true, GRPC_ARG_ENABLE_PER_MESSAGE_DECOMPRESSION,
           &MessageDecompressFilter);
  required(GRPC_CLIENT_SUBCHANNEL, &grpc_http_client_filter);
  required(GRPC_CLIENT_DIRECT_CHANNEL, &grpc_http_client_filter);
  required(GRPC_SERVER_CHANNEL, &grpc_http_server_filter);
}
}  // namespace grpc_core
