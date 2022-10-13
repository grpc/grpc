//
//
// Copyright 2020 gRPC authors.
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
//

#include <stddef.h>

#include "absl/status/statusor.h"

#include <grpc/impl/codegen/compression_types.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/transport/transport.h"
#ifndef GRPC_CORE_EXT_FILTERS_HTTP_MESSAGE_COMPRESS_MESSAGE_DECOMPRESS_FILTER_H
#define GRPC_CORE_EXT_FILTERS_HTTP_MESSAGE_COMPRESS_MESSAGE_DECOMPRESS_FILTER_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"

namespace grpc_core {

class MessageDecompressFilter : public ChannelFilter {
 protected:
  explicit MessageDecompressFilter(const ChannelArgs& args);

  auto DecompressLoop(grpc_compression_algorithm algorithm,
                      PipeSender<MessageHandle>* decompressed,
                      PipeReceiver<MessageHandle>* compressed) const;

 private:
  int max_recv_size_;
  size_t message_size_service_config_parser_index_;
};

class ClientMessageDecompressFilter final : public MessageDecompressFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<ClientMessageDecompressFilter> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  // Construct a promise for one call.
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;

 private:
  using MessageDecompressFilter::MessageDecompressFilter;
};

class ServerMessageDecompressFilter final : public MessageDecompressFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<ServerMessageDecompressFilter> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  // Construct a promise for one call.
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;

 private:
  using MessageDecompressFilter::MessageDecompressFilter;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_HTTP_MESSAGE_COMPRESS_MESSAGE_DECOMPRESS_FILTER_H \
        */
