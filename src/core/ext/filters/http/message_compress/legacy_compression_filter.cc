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

#include "src/core/ext/filters/http/message_compress/legacy_compression_filter.h"

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

const grpc_channel_filter LegacyClientCompressionFilter::kFilter =
    MakePromiseBasedFilter<
        LegacyClientCompressionFilter, FilterEndpoint::kClient,
        kFilterExaminesServerInitialMetadata | kFilterExaminesInboundMessages |
            kFilterExaminesOutboundMessages>("compression");
const grpc_channel_filter LegacyServerCompressionFilter::kFilter =
    MakePromiseBasedFilter<
        LegacyServerCompressionFilter, FilterEndpoint::kServer,
        kFilterExaminesServerInitialMetadata | kFilterExaminesInboundMessages |
            kFilterExaminesOutboundMessages>("compression");

absl::StatusOr<LegacyClientCompressionFilter>
LegacyClientCompressionFilter::Create(const ChannelArgs& args,
                                      ChannelFilter::Args) {
  return LegacyClientCompressionFilter(args);
}

absl::StatusOr<LegacyServerCompressionFilter>
LegacyServerCompressionFilter::Create(const ChannelArgs& args,
                                      ChannelFilter::Args) {
  return LegacyServerCompressionFilter(args);
}

LegacyCompressionFilter::LegacyCompressionFilter(const ChannelArgs& args)
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

MessageHandle LegacyCompressionFilter::CompressMessage(
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

absl::StatusOr<MessageHandle> LegacyCompressionFilter::DecompressMessage(
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

grpc_compression_algorithm LegacyCompressionFilter::HandleOutgoingMetadata(
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

LegacyCompressionFilter::DecompressArgs
LegacyCompressionFilter::HandleIncomingMetadata(
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
    max_recv_message_length = *limits->max_recv_size();
  }
  return DecompressArgs{incoming_metadata.get(GrpcEncodingMetadata())
                            .value_or(GRPC_COMPRESS_NONE),
                        max_recv_message_length};
}

ArenaPromise<ServerMetadataHandle>
LegacyClientCompressionFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  auto compression_algorithm =
      HandleOutgoingMetadata(*call_args.client_initial_metadata);
  call_args.client_to_server_messages->InterceptAndMap(
      [compression_algorithm,
       this](MessageHandle message) -> absl::optional<MessageHandle> {
        return CompressMessage(std::move(message), compression_algorithm);
      });
  auto* decompress_args = GetContext<Arena>()->New<DecompressArgs>(
      DecompressArgs{GRPC_COMPRESS_ALGORITHMS_COUNT, absl::nullopt});
  auto* decompress_err =
      GetContext<Arena>()->New<Latch<ServerMetadataHandle>>();
  call_args.server_initial_metadata->InterceptAndMap(
      [decompress_args, this](ServerMetadataHandle server_initial_metadata)
          -> absl::optional<ServerMetadataHandle> {
        if (server_initial_metadata == nullptr) return absl::nullopt;
        *decompress_args = HandleIncomingMetadata(*server_initial_metadata);
        return std::move(server_initial_metadata);
      });
  call_args.server_to_client_messages->InterceptAndMap(
      [decompress_err, decompress_args,
       this](MessageHandle message) -> absl::optional<MessageHandle> {
        auto r = DecompressMessage(std::move(message), *decompress_args);
        if (!r.ok()) {
          decompress_err->Set(ServerMetadataFromStatus(r.status()));
          return absl::nullopt;
        }
        return std::move(*r);
      });
  // Run the next filter, and race it with getting an error from decompression.
  return PrioritizedRace(decompress_err->Wait(),
                         next_promise_factory(std::move(call_args)));
}

ArenaPromise<ServerMetadataHandle>
LegacyServerCompressionFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  auto decompress_args =
      HandleIncomingMetadata(*call_args.client_initial_metadata);
  auto* decompress_err =
      GetContext<Arena>()->New<Latch<ServerMetadataHandle>>();
  call_args.client_to_server_messages->InterceptAndMap(
      [decompress_err, decompress_args,
       this](MessageHandle message) -> absl::optional<MessageHandle> {
        auto r = DecompressMessage(std::move(message), decompress_args);
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_DEBUG, "%s[compression] DecompressMessage returned %s",
                  GetContext<Activity>()->DebugTag().c_str(),
                  r.status().ToString().c_str());
        }
        if (!r.ok()) {
          decompress_err->Set(ServerMetadataFromStatus(r.status()));
          return absl::nullopt;
        }
        return std::move(*r);
      });
  auto* compression_algorithm =
      GetContext<Arena>()->New<grpc_compression_algorithm>();
  call_args.server_initial_metadata->InterceptAndMap(
      [this, compression_algorithm](ServerMetadataHandle md) {
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_INFO, "%s[compression] Write metadata",
                  GetContext<Activity>()->DebugTag().c_str());
        }
        // Find the compression algorithm.
        *compression_algorithm = HandleOutgoingMetadata(*md);
        return md;
      });
  call_args.server_to_client_messages->InterceptAndMap(
      [compression_algorithm,
       this](MessageHandle message) -> absl::optional<MessageHandle> {
        return CompressMessage(std::move(message), *compression_algorithm);
      });
  // Run the next filter, and race it with getting an error from decompression.
  return PrioritizedRace(decompress_err->Wait(),
                         next_promise_factory(std::move(call_args)));
}

}  // namespace grpc_core
