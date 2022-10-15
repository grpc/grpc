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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/http/message_compress/compression_filter.h"

#include <stdint.h>

#include <functional>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"

#include <grpc/compression.h>
#include <grpc/impl/codegen/compression_types.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/http/message_compress/compression_filter.h"
#include "src/core/ext/filters/message_size/message_size_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_concurrently.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

const grpc_channel_filter ClientCompressionFilter::kFilter =
    MakePromiseBasedFilter<ClientCompressionFilter, FilterEndpoint::kClient,
                           kFilterExaminesServerInitialMetadata |
                               kFilterExaminesInboundMessages |
                               kFilterExaminesOutboundMessages>("compression");
const grpc_channel_filter ServerCompressionFilter::kFilter =
    MakePromiseBasedFilter<ServerCompressionFilter, FilterEndpoint::kServer,
                           kFilterExaminesServerInitialMetadata |
                               kFilterExaminesInboundMessages |
                               kFilterExaminesOutboundMessages>("compression");

absl::StatusOr<ClientCompressionFilter> ClientCompressionFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  return ClientCompressionFilter(args);
}

absl::StatusOr<ServerCompressionFilter> ServerCompressionFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  return ServerCompressionFilter(args);
}

CompressionFilter::CompressionFilter(const ChannelArgs& args)
    : max_recv_size_(GetMaxRecvSizeFromChannelArgs(args)),
      message_size_service_config_parser_index_(
          MessageSizeParser::ParserIndex()),
      default_compression_algorithm_(
          DefaultCompressionAlgorithmFromChannelArgs(args).value_or(
              GRPC_COMPRESS_NONE)),
      enabled_compression_algorithms_(
          CompressionAlgorithmSet::FromChannelArgs(args)),
      enable_compression_(
          args.GetBool(GRPC_ARG_ENABLE_PER_MESSAGE_COMPRESSION).value_or(true)),
      enable_decompression_(
          args.GetBool(GRPC_ARG_ENABLE_PER_MESSAGE_DECOMPRESSION)
              .value_or(true)) {
  // Make sure the default is enabled.
  if (!enabled_compression_algorithms_.IsSet(default_compression_algorithm_)) {
    const char* name;
    if (!grpc_compression_algorithm_name(default_compression_algorithm_,
                                         &name)) {
      name = "<unknown>";
    }
    gpr_log(GPR_ERROR,
            "default compression algorithm %s not enabled: switching to none",
            name);
    default_compression_algorithm_ = GRPC_COMPRESS_NONE;
  }
}

MessageHandle CompressionFilter::CompressMessage(
    MessageHandle message, grpc_compression_algorithm algorithm) const {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_compression_trace)) {
    gpr_log(GPR_ERROR, "CompressMessage: len=%d alg=%d flags=%d",
            (int)message->payload()->Length(), algorithm, message->flags());
  }
  uint32_t& flags = message->mutable_flags();
  if (algorithm == GRPC_COMPRESS_NONE || !enable_compression_ ||
      (flags & (GRPC_WRITE_NO_COMPRESS | GRPC_WRITE_INTERNAL_COMPRESS))) {
    return message;
  }
  SliceBuffer tmp;
  SliceBuffer* payload = message->payload();
  bool did_compress = grpc_msg_compress(algorithm, payload->c_slice_buffer(),
                                        tmp.c_slice_buffer());
  if (did_compress) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_compression_trace)) {
      const char* algo_name;
      const size_t before_size = payload->Length();
      const size_t after_size = tmp.Length();
      const float savings_ratio = 1.0f - static_cast<float>(after_size) /
                                             static_cast<float>(before_size);
      GPR_ASSERT(grpc_compression_algorithm_name(algorithm, &algo_name));
      gpr_log(GPR_INFO,
              "Compressed[%s] %" PRIuPTR " bytes vs. %" PRIuPTR
              " bytes (%.2f%% savings)",
              algo_name, before_size, after_size, 100 * savings_ratio);
    }
    tmp.Swap(payload);
    flags |= GRPC_WRITE_INTERNAL_COMPRESS;
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_compression_trace)) {
      const char* algo_name;
      GPR_ASSERT(grpc_compression_algorithm_name(algorithm, &algo_name));
      gpr_log(GPR_INFO,
              "Algorithm '%s' enabled but decided not to compress. Input size: "
              "%" PRIuPTR,
              algo_name, payload->Length());
    }
  }
  return message;
}

absl::StatusOr<MessageHandle> CompressionFilter::DecompressMessage(
    MessageHandle message, grpc_compression_algorithm algorithm,
    int max_recv_message_length) const {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_compression_trace)) {
    gpr_log(GPR_ERROR, "DecompressMessage: len=%d max=%d alg=%d",
            (int)message->payload()->Length(), max_recv_message_length,
            algorithm);
  }
  if (max_recv_message_length > 0 &&
      message->payload()->Length() > max_recv_message_length) {
    return absl::ResourceExhaustedError(
        absl::StrFormat("Received message larger than max (%u vs. %d)",
                        message->payload()->Length(), max_recv_message_length));
  }
  if (!enable_decompression_ ||
      (message->flags() & GRPC_WRITE_INTERNAL_COMPRESS) == 0) {
    return std::move(message);
  }
  SliceBuffer decompressed_slices;
  if (grpc_msg_decompress(algorithm, message->payload()->c_slice_buffer(),
                          decompressed_slices.c_slice_buffer()) == 0) {
    return absl::InternalError(
        absl::StrCat("Unexpected error decompressing data for algorithm ",
                     CompressionAlgorithmAsString(algorithm)));
  }
  message->payload()->Swap(&decompressed_slices);
  message->mutable_flags() &= ~GRPC_WRITE_INTERNAL_COMPRESS;
  return std::move(message);
}

auto CompressionFilter::DecompressLoop(
    grpc_compression_algorithm algorithm,
    PipeReceiver<MessageHandle>* compressed,
    PipeSender<MessageHandle>* decompressed) const {
  auto max_recv_message_length = max_recv_size_;
  const MessageSizeParsedConfig* limits =
      MessageSizeParsedConfig::GetFromCallContext(
          GetContext<grpc_call_context_element>(),
          message_size_service_config_parser_index_);
  if (limits != nullptr && limits->limits().max_recv_size >= 0 &&
      (limits->limits().max_recv_size < max_recv_message_length ||
       max_recv_message_length < 0)) {
    max_recv_message_length = limits->limits().max_recv_size;
  }
  return ForEach(std::move(*compressed),
                 [decompressed, algorithm, max_recv_message_length,
                  this](MessageHandle message) {
                   return TrySeq(
                       [message = std::move(message), algorithm,
                        max_recv_message_length, this]() mutable {
                         return DecompressMessage(std::move(message), algorithm,
                                                  max_recv_message_length);
                       },
                       [decompressed](MessageHandle message) {
                         return decompressed->Push(std::move(message));
                       },
                       [](bool successful_push) {
                         if (successful_push) {
                           return absl::OkStatus();
                         }
                         return absl::CancelledError();
                       });
                 });
}

auto CompressionFilter::CompressLoop(
    grpc_metadata_batch* md, PipeReceiver<MessageHandle>* uncompressed,
    PipeSender<MessageHandle>* compressed) const {
  const auto algorithm = md->Take(GrpcInternalEncodingRequest())
                             .value_or(default_compression_algorithm());
  // Convey supported compression algorithms.
  md->Set(GrpcAcceptEncodingMetadata(), enabled_compression_algorithms());
  if (algorithm != GRPC_COMPRESS_NONE) {
    md->Set(GrpcEncodingMetadata(), algorithm);
  }
  return ForEach(std::move(*uncompressed), [compressed, algorithm,
                                            this](MessageHandle message) {
    return Seq(compressed->Push(CompressMessage(std::move(message), algorithm)),
               [](bool successful_push) {
                 if (successful_push) {
                   return absl::OkStatus();
                 }
                 return absl::CancelledError();
               });
  });
}

ArenaPromise<ServerMetadataHandle> ClientCompressionFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  auto* server_initial_metadata = call_args.server_initial_metadata;
  auto* decompress_pipe = GetContext<Arena>()->New<Pipe<MessageHandle>>();
  auto* decompress_sender =
      std::exchange(call_args.incoming_messages, &decompress_pipe->sender);
  auto* decompress_receiver = &decompress_pipe->receiver;
  auto* compress_pipe = GetContext<Arena>()->New<Pipe<MessageHandle>>();
  auto* compress_sender = &compress_pipe->sender;
  auto* compress_receiver =
      std::exchange(call_args.outgoing_messages, &compress_pipe->receiver);
  auto compress_loop = CompressLoop(call_args.client_initial_metadata.get(),
                                    compress_receiver, compress_sender);
  return TryConcurrently(next_promise_factory(std::move(call_args)))
      .Pull(Seq(server_initial_metadata->Wait(),
                [this, decompress_receiver,
                 decompress_sender](ServerMetadata** server_initial_metadata)
                    -> ArenaPromise<absl::Status> {
                  if (*server_initial_metadata == nullptr) {
                    return ImmediateOkStatus();
                  }
                  const auto decompress_algorithm =
                      (*server_initial_metadata)
                          ->get(GrpcEncodingMetadata())
                          .value_or(GRPC_COMPRESS_NONE);
                  return DecompressLoop(decompress_algorithm,
                                        decompress_receiver, decompress_sender);
                }))
      .Push(std::move(compress_loop));
}

ArenaPromise<ServerMetadataHandle> ServerCompressionFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  const auto decompress_algorithm =
      call_args.client_initial_metadata->get(GrpcEncodingMetadata())
          .value_or(GRPC_COMPRESS_NONE);
  auto* decompress_pipe = GetContext<Arena>()->New<Pipe<MessageHandle>>();
  auto* decompress_sender =
      std::exchange(call_args.incoming_messages, &decompress_pipe->sender);
  auto* decompress_receiver = &decompress_pipe->receiver;
  auto* compress_pipe = GetContext<Arena>()->New<Pipe<MessageHandle>>();
  auto* compress_sender = &compress_pipe->sender;
  auto* compress_receiver =
      std::exchange(call_args.outgoing_messages, &compress_pipe->receiver);
  auto* read_latch = GetContext<Arena>()->New<Latch<ServerMetadata*>>();
  auto* write_latch =
      std::exchange(call_args.server_initial_metadata, read_latch);
  return TryConcurrently(next_promise_factory(std::move(call_args)))
      .Pull(DecompressLoop(decompress_algorithm, decompress_receiver,
                           decompress_sender))
      .Push(Seq(read_latch->Wait(), [write_latch, this, compress_receiver,
                                     compress_sender](ServerMetadata** md) {
        // Find the compression algorithm.
        auto loop = CompressLoop(*md, compress_receiver, compress_sender);
        write_latch->Set(*md);
        return loop;
      }));
}

}  // namespace grpc_core
