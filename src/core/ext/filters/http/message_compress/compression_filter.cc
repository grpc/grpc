// Copyright 2022 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/http/message_compress/compression_filter.h"

#include <inttypes.h>

#include <functional>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/compression_types.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/message_size/message_size_filter.h"
#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/prioritized_race.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/call_trace.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

const NoInterceptor ServerCompressionFilter::Call::OnServerTrailingMetadata;
const NoInterceptor ServerCompressionFilter::Call::OnFinalize;
const NoInterceptor ClientCompressionFilter::Call::OnServerTrailingMetadata;
const NoInterceptor ClientCompressionFilter::Call::OnFinalize;

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

ChannelCompression::ChannelCompression(const ChannelArgs& args)
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

MessageHandle ChannelCompression::CompressMessage(
    MessageHandle message, grpc_compression_algorithm algorithm) const {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_compression_trace)) {
    gpr_log(GPR_INFO, "CompressMessage: len=%" PRIdPTR " alg=%d flags=%d",
            message->payload()->Length(), algorithm, message->flags());
  }
  auto* call_context = GetContext<grpc_call_context_element>();
  auto* call_tracer = static_cast<CallTracerInterface*>(
      call_context[GRPC_CONTEXT_CALL_TRACER].value);
  if (call_tracer != nullptr) {
    call_tracer->RecordSendMessage(*message->payload());
  }
  // Check if we're allowed to compress this message
  // (apps might want to disable compression for certain messages to avoid
  // crime/beast like vulns).
  uint32_t& flags = message->mutable_flags();
  if (algorithm == GRPC_COMPRESS_NONE || !enable_compression_ ||
      (flags & (GRPC_WRITE_NO_COMPRESS | GRPC_WRITE_INTERNAL_COMPRESS))) {
    return message;
  }
  // Try to compress the payload.
  SliceBuffer tmp;
  SliceBuffer* payload = message->payload();
  bool did_compress = grpc_msg_compress(algorithm, payload->c_slice_buffer(),
                                        tmp.c_slice_buffer());
  // If we achieved compression send it as compressed, otherwise send it as (to
  // avoid spending cycles on the receiver decompressing).
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
    if (call_tracer != nullptr) {
      call_tracer->RecordSendCompressedMessage(*message->payload());
    }
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

absl::StatusOr<MessageHandle> ChannelCompression::DecompressMessage(
    MessageHandle message, DecompressArgs args) const {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_compression_trace)) {
    gpr_log(GPR_INFO, "DecompressMessage: len=%" PRIdPTR " max=%d alg=%d",
            message->payload()->Length(),
            args.max_recv_message_length.value_or(-1), args.algorithm);
  }
  auto* call_context = GetContext<grpc_call_context_element>();
  auto* call_tracer = static_cast<CallTracerInterface*>(
      call_context[GRPC_CONTEXT_CALL_TRACER].value);
  if (call_tracer != nullptr) {
    call_tracer->RecordReceivedMessage(*message->payload());
  }
  // Check max message length.
  if (args.max_recv_message_length.has_value() &&
      message->payload()->Length() >
          static_cast<size_t>(*args.max_recv_message_length)) {
    return absl::ResourceExhaustedError(absl::StrFormat(
        "Received message larger than max (%u vs. %d)",
        message->payload()->Length(), *args.max_recv_message_length));
  }
  // Check if decompression is enabled (if not, we can just pass the message
  // up).
  if (!enable_decompression_ ||
      (message->flags() & GRPC_WRITE_INTERNAL_COMPRESS) == 0) {
    return std::move(message);
  }
  // Try to decompress the payload.
  SliceBuffer decompressed_slices;
  if (grpc_msg_decompress(args.algorithm, message->payload()->c_slice_buffer(),
                          decompressed_slices.c_slice_buffer()) == 0) {
    return absl::InternalError(
        absl::StrCat("Unexpected error decompressing data for algorithm ",
                     CompressionAlgorithmAsString(args.algorithm)));
  }
  // Swap the decompressed slices into the message.
  message->payload()->Swap(&decompressed_slices);
  message->mutable_flags() &= ~GRPC_WRITE_INTERNAL_COMPRESS;
  message->mutable_flags() |= GRPC_WRITE_INTERNAL_TEST_ONLY_WAS_COMPRESSED;
  if (call_tracer != nullptr) {
    call_tracer->RecordReceivedDecompressedMessage(*message->payload());
  }
  return std::move(message);
}

grpc_compression_algorithm ChannelCompression::HandleOutgoingMetadata(
    grpc_metadata_batch& outgoing_metadata) {
  const auto algorithm = outgoing_metadata.Take(GrpcInternalEncodingRequest())
                             .value_or(default_compression_algorithm());
  // Convey supported compression algorithms.
  outgoing_metadata.Set(GrpcAcceptEncodingMetadata(),
                        enabled_compression_algorithms());
  if (algorithm != GRPC_COMPRESS_NONE) {
    outgoing_metadata.Set(GrpcEncodingMetadata(), algorithm);
  }
  return algorithm;
}

ChannelCompression::DecompressArgs ChannelCompression::HandleIncomingMetadata(
    const grpc_metadata_batch& incoming_metadata) {
  // Configure max receive size.
  auto max_recv_message_length = max_recv_size_;
  const MessageSizeParsedConfig* limits =
      MessageSizeParsedConfig::GetFromCallContext(
          GetContext<grpc_call_context_element>(),
          message_size_service_config_parser_index_);
  if (limits != nullptr && limits->max_recv_size().has_value() &&
      (!max_recv_message_length.has_value() ||
       *limits->max_recv_size() < *max_recv_message_length)) {
    max_recv_message_length = limits->max_recv_size();
  }
  return DecompressArgs{incoming_metadata.get(GrpcEncodingMetadata())
                            .value_or(GRPC_COMPRESS_NONE),
                        max_recv_message_length};
}

void ClientCompressionFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, ClientCompressionFilter* filter) {
  compression_algorithm_ =
      filter->compression_engine_.HandleOutgoingMetadata(md);
}

MessageHandle ClientCompressionFilter::Call::OnClientToServerMessage(
    MessageHandle message, ClientCompressionFilter* filter) {
  return filter->compression_engine_.CompressMessage(std::move(message),
                                                     compression_algorithm_);
}

void ClientCompressionFilter::Call::OnServerInitialMetadata(
    ServerMetadata& md, ClientCompressionFilter* filter) {
  decompress_args_ = filter->compression_engine_.HandleIncomingMetadata(md);
}

absl::StatusOr<MessageHandle>
ClientCompressionFilter::Call::OnServerToClientMessage(
    MessageHandle message, ClientCompressionFilter* filter) {
  return filter->compression_engine_.DecompressMessage(std::move(message),
                                                       decompress_args_);
}

void ServerCompressionFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, ServerCompressionFilter* filter) {
  decompress_args_ = filter->compression_engine_.HandleIncomingMetadata(md);
}

absl::StatusOr<MessageHandle>
ServerCompressionFilter::Call::OnClientToServerMessage(
    MessageHandle message, ServerCompressionFilter* filter) {
  return filter->compression_engine_.DecompressMessage(std::move(message),
                                                       decompress_args_);
}

void ServerCompressionFilter::Call::OnServerInitialMetadata(
    ServerMetadata& md, ServerCompressionFilter* filter) {
  compression_algorithm_ =
      filter->compression_engine_.HandleOutgoingMetadata(md);
}

MessageHandle ServerCompressionFilter::Call::OnServerToClientMessage(
    MessageHandle message, ServerCompressionFilter* filter) {
  return filter->compression_engine_.CompressMessage(std::move(message),
                                                     compression_algorithm_);
}

}  // namespace grpc_core
