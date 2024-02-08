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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_HTTP_MESSAGE_COMPRESS_COMPRESSION_FILTER_H
#define GRPC_SRC_CORE_EXT_FILTERS_HTTP_MESSAGE_COMPRESS_COMPRESSION_FILTER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include <grpc/impl/compression_types.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

/// Compression filter for messages.
///
/// See <grpc/compression.h> for the available compression settings.
///
/// Compression settings may come from:
/// - Channel configuration, as established at channel creation time.
/// - The metadata accompanying the outgoing data to be compressed. This is
///   taken as a request only. We may choose not to honor it. The metadata key
///   is given by \a GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY.
///
/// Compression can be disabled for concrete messages (for instance in order to
/// prevent CRIME/BEAST type attacks) by having the GRPC_WRITE_NO_COMPRESS set
/// in the MessageHandle flags.
///
/// The attempted compression mechanism is added to the resulting initial
/// metadata under the 'grpc-encoding' key.
///
/// If compression is actually performed, the MessageHandle's flag is modified
/// to incorporate GRPC_WRITE_INTERNAL_COMPRESS. Otherwise, and regardless of
/// the aforementioned 'grpc-encoding' metadata value, data will pass through
/// uncompressed.

class ChannelCompression {
 public:
  explicit ChannelCompression(const ChannelArgs& args);

  struct DecompressArgs {
    grpc_compression_algorithm algorithm;
    absl::optional<uint32_t> max_recv_message_length;
  };

  grpc_compression_algorithm default_compression_algorithm() const {
    return default_compression_algorithm_;
  }

  CompressionAlgorithmSet enabled_compression_algorithms() const {
    return enabled_compression_algorithms_;
  }

  grpc_compression_algorithm HandleOutgoingMetadata(
      grpc_metadata_batch& outgoing_metadata);
  DecompressArgs HandleIncomingMetadata(
      const grpc_metadata_batch& incoming_metadata);

  // Compress one message synchronously.
  MessageHandle CompressMessage(MessageHandle message,
                                grpc_compression_algorithm algorithm) const;
  // Decompress one message synchronously.
  absl::StatusOr<MessageHandle> DecompressMessage(MessageHandle message,
                                                  DecompressArgs args) const;

 private:
  // Max receive message length, if set.
  absl::optional<uint32_t> max_recv_size_;
  size_t message_size_service_config_parser_index_;
  // The default, channel-level, compression algorithm.
  grpc_compression_algorithm default_compression_algorithm_;
  // Enabled compression algorithms.
  CompressionAlgorithmSet enabled_compression_algorithms_;
  // Is compression enabled?
  bool enable_compression_;
  // Is decompression enabled?
  bool enable_decompression_;
};

class ClientCompressionFilter final
    : public ImplementChannelFilter<ClientCompressionFilter> {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<ClientCompressionFilter> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  // Construct a promise for one call.
  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata& md,
                                 ClientCompressionFilter* filter);
    MessageHandle OnClientToServerMessage(MessageHandle message,
                                          ClientCompressionFilter* filter);

    void OnServerInitialMetadata(ServerMetadata& md,
                                 ClientCompressionFilter* filter);
    absl::StatusOr<MessageHandle> OnServerToClientMessage(
        MessageHandle message, ClientCompressionFilter* filter);

    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnFinalize;

   private:
    grpc_compression_algorithm compression_algorithm_;
    ChannelCompression::DecompressArgs decompress_args_;
  };

 private:
  explicit ClientCompressionFilter(const ChannelArgs& args)
      : compression_engine_(args) {}

  ChannelCompression compression_engine_;
};

class ServerCompressionFilter final
    : public ImplementChannelFilter<ServerCompressionFilter> {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<ServerCompressionFilter> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  // Construct a promise for one call.
  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata& md,
                                 ServerCompressionFilter* filter);
    absl::StatusOr<MessageHandle> OnClientToServerMessage(
        MessageHandle message, ServerCompressionFilter* filter);

    void OnServerInitialMetadata(ServerMetadata& md,
                                 ServerCompressionFilter* filter);
    MessageHandle OnServerToClientMessage(MessageHandle message,
                                          ServerCompressionFilter* filter);

    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnFinalize;

   private:
    ChannelCompression::DecompressArgs decompress_args_;
    grpc_compression_algorithm compression_algorithm_;
  };

 private:
  explicit ServerCompressionFilter(const ChannelArgs& args)
      : compression_engine_(args) {}

  ChannelCompression compression_engine_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_HTTP_MESSAGE_COMPRESS_COMPRESSION_FILTER_H
