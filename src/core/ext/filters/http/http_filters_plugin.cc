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

#include "absl/types/optional.h"

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/ext/filters/http/client/http_client_filter.h"
#include "src/core/ext/filters/http/message_compress/message_compress_filter.h"
#include "src/core/ext/filters/http/message_compress/message_decompress_filter.h"
#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport_fwd.h"
#include "src/core/lib/transport/transport_impl.h"

static bool is_building_http_like_transport(
    grpc_core::ChannelStackBuilder* builder) {
  grpc_transport* t = builder->transport();
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
         filter](ChannelStackBuilder* builder) {
          if (!is_building_http_like_transport(builder)) return true;
          auto args = builder->channel_args();
          const bool enable = args.GetBool(control_channel_arg)
                                  .value_or(enable_in_minimal_stack ||
                                            !args.WantMinimalStack());
          if (enable) builder->PrependFilter(filter);
          return true;
        });
  };
  auto required = [builder](grpc_channel_stack_type channel_type,
                            const grpc_channel_filter* filter) {
    builder->channel_init()->RegisterStage(
        channel_type, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
        [filter](ChannelStackBuilder* builder) {
          if (is_building_http_like_transport(builder)) {
            builder->PrependFilter(filter);
          }
          return true;
        });
  };
  // TODO(ctiller): return this flag to true once the promise conversion is
  // complete.
  static constexpr bool kMinimalStackHasDecompression = false;
  optional(GRPC_CLIENT_SUBCHANNEL, false,
           GRPC_ARG_ENABLE_PER_MESSAGE_COMPRESSION,
           &grpc_message_compress_filter);
  optional(GRPC_CLIENT_DIRECT_CHANNEL, false,
           GRPC_ARG_ENABLE_PER_MESSAGE_COMPRESSION,
           &grpc_message_compress_filter);
  optional(GRPC_SERVER_CHANNEL, false, GRPC_ARG_ENABLE_PER_MESSAGE_COMPRESSION,
           &grpc_message_compress_filter);
  optional(GRPC_CLIENT_SUBCHANNEL, kMinimalStackHasDecompression,
           GRPC_ARG_ENABLE_PER_MESSAGE_DECOMPRESSION, &MessageDecompressFilter);
  optional(GRPC_CLIENT_DIRECT_CHANNEL, kMinimalStackHasDecompression,
           GRPC_ARG_ENABLE_PER_MESSAGE_DECOMPRESSION, &MessageDecompressFilter);
  optional(GRPC_SERVER_CHANNEL, kMinimalStackHasDecompression,
           GRPC_ARG_ENABLE_PER_MESSAGE_DECOMPRESSION, &MessageDecompressFilter);
  required(GRPC_CLIENT_SUBCHANNEL, &HttpClientFilter::kFilter);
  required(GRPC_CLIENT_DIRECT_CHANNEL, &HttpClientFilter::kFilter);
  required(GRPC_SERVER_CHANNEL, &HttpServerFilter::kFilter);
}
}  // namespace grpc_core
