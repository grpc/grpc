/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/ext/filters/http/message_compress/message_compress_filter.h"

#include <inttypes.h>
#include <stdlib.h>

#include <new>
#include <utility>

#include "absl/meta/type_traits.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"

#include <grpc/compression.h>
#include <grpc/impl/codegen/compression_types.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>

#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/promise/call_push_pull.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

const grpc_channel_filter MessageCompressFilter::kClientFilter =
    MakePromiseBasedFilter<MessageCompressFilter, FilterEndpoint::kClient,
                           kFilterExaminesOutboundMessages>("message_compress");
const grpc_channel_filter MessageCompressFilter::kServerFilter =
    MakePromiseBasedFilter<MessageCompressFilter, FilterEndpoint::kServer,
                           kFilterExaminesOutboundMessages>("message_compress");

absl::StatusOr<MessageCompressFilter> MessageCompressFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  return MessageCompressFilter(args);
}

MessageCompressFilter::MessageCompressFilter(const ChannelArgs& args)
    : default_compression_algorithm_(
          DefaultCompressionAlgorithmFromChannelArgs(args).value_or(
              GRPC_COMPRESS_NONE)),
      enabled_compression_algorithms_(
          CompressionAlgorithmSet::FromChannelArgs(args)) {
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

namespace {
MessageHandle CompressMessage(MessageHandle message,
                              grpc_compression_algorithm algorithm) {
  GPR_ASSERT(algorithm != GRPC_COMPRESS_NONE);
  uint32_t& flags = message->mutable_flags();
  if (flags & (GRPC_WRITE_NO_COMPRESS | GRPC_WRITE_INTERNAL_COMPRESS)) {
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
}  // namespace

ArenaPromise<ServerMetadataHandle> MessageCompressFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  // Find the compression algorithm.
  const auto algorithm =
      call_args.client_initial_metadata->Take(GrpcInternalEncodingRequest())
          .value_or(default_compression_algorithm_);
  // Convey supported compression algorithms.
  call_args.client_initial_metadata->Set(GrpcAcceptEncodingMetadata(),
                                         enabled_compression_algorithms_);
  switch (algorithm) {
    case GRPC_COMPRESS_ALGORITHMS_COUNT:
      abort();
    case GRPC_COMPRESS_NONE:
      // No compression, we just pass through.
      return next_promise_factory(std::move(call_args));
    default: {
      call_args.client_initial_metadata->Set(GrpcEncodingMetadata(), algorithm);
      auto* pipe = GetContext<Arena>()->New<Pipe<MessageHandle>>();
      auto* sender = &pipe->sender;
      auto* receiver =
          std::exchange(call_args.outgoing_messages, &pipe->receiver);
      return CallPushPull(next_promise_factory(std::move(call_args)),
                          ForEach(std::move(*receiver),
                                  [sender, algorithm](MessageHandle message) {
                                    return Seq(
                                        sender->Push(CompressMessage(
                                            std::move(message), algorithm)),
                                        [](bool successful_push) {
                                          if (successful_push) {
                                            return absl::OkStatus();
                                          }
                                          return absl::CancelledError();
                                        });
                                  }),
                          []() { return absl::OkStatus(); });
    }
  }
}

}  // namespace grpc_core
