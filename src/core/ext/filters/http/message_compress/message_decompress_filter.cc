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

#include "src/core/ext/filters/http/message_compress/message_decompress_filter.h"

#include <stdint.h>
#include <string.h>

#include <new>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"

#include <grpc/impl/codegen/compression_types.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/message_size/message_size_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_concurrently.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

const grpc_channel_filter ClientMessageDecompressFilter::kFilter =
    MakePromiseBasedFilter<
        ClientMessageDecompressFilter, FilterEndpoint::kClient,
        kFilterExaminesServerInitialMetadata | kFilterExaminesInboundMessages>(
        "message_decompress");
const grpc_channel_filter ServerMessageDecompressFilter::kFilter =
    MakePromiseBasedFilter<ServerMessageDecompressFilter,
                           FilterEndpoint::kServer,
                           kFilterExaminesInboundMessages>(
        "message_decompress");

absl::StatusOr<ClientMessageDecompressFilter>
ClientMessageDecompressFilter::Create(const ChannelArgs& args,
                                      ChannelFilter::Args) {
  return ClientMessageDecompressFilter(args);
}

absl::StatusOr<ServerMessageDecompressFilter>
ServerMessageDecompressFilter::Create(const ChannelArgs& args,
                                      ChannelFilter::Args) {
  return ServerMessageDecompressFilter(args);
}

MessageDecompressFilter::MessageDecompressFilter(const ChannelArgs& args)
    : max_recv_size_(GetMaxRecvSizeFromChannelArgs(args)),
      message_size_service_config_parser_index_(
          MessageSizeParser::ParserIndex()) {}

absl::StatusOr<MessageHandle> DecompressMessage(
    MessageHandle message, grpc_compression_algorithm algorithm,
    int max_recv_message_length) {
  gpr_log(GPR_ERROR, "DecompressMessage: %d %d",
          (int)message->payload()->Length(), max_recv_message_length);
  if (max_recv_message_length > 0 &&
      message->payload()->Length() > max_recv_message_length) {
    gpr_log(GPR_ERROR, "RETURN ERROR");
    return absl::ResourceExhaustedError(
        absl::StrFormat("Received message larger than max (%u vs. %d)",
                        message->payload()->Length(), max_recv_message_length));
  }
  if ((message->flags() & GRPC_WRITE_INTERNAL_COMPRESS) == 0) {
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

auto MessageDecompressFilter::DecompressLoop(
    grpc_compression_algorithm algorithm,
    PipeSender<MessageHandle>* decompressed,
    PipeReceiver<MessageHandle>* compressed) const {
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
  gpr_log(GPR_ERROR, "MAX_RECV_MESSAGE_LENGTH:%d", max_recv_message_length);
  return ForEach(std::move(*compressed),
                 [decompressed, algorithm,
                  max_recv_message_length](MessageHandle message) {
                   return TrySeq(
                       [message = std::move(message), algorithm,
                        max_recv_message_length]() mutable {
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

ArenaPromise<ServerMetadataHandle>
ClientMessageDecompressFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  auto* server_initial_metadata = call_args.server_initial_metadata;
  auto* pipe = GetContext<Arena>()->New<Pipe<MessageHandle>>();
  auto* sender = std::exchange(call_args.incoming_messages, &pipe->sender);
  auto* receiver = &pipe->receiver;
  return TryConcurrently(next_promise_factory(std::move(call_args)))
      .HelperPull(
          Seq(server_initial_metadata->Wait(),
              [this, receiver, sender](ServerMetadata** server_initial_metadata)
                  -> ArenaPromise<absl::Status> {
                if (server_initial_metadata == nullptr)
                  return ImmediateOkStatus();
                const auto algorithm = (*server_initial_metadata)
                                           ->get(GrpcEncodingMetadata())
                                           .value_or(GRPC_COMPRESS_NONE);
                return DecompressLoop(algorithm, sender, receiver);
              }));
}

ArenaPromise<ServerMetadataHandle>
ServerMessageDecompressFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  const auto algorithm =
      call_args.client_initial_metadata->get(GrpcEncodingMetadata())
          .value_or(GRPC_COMPRESS_NONE);
  auto* pipe = GetContext<Arena>()->New<Pipe<MessageHandle>>();
  auto* sender = std::exchange(call_args.incoming_messages, &pipe->sender);
  auto* receiver = &pipe->receiver;
  return TryConcurrently(next_promise_factory(std::move(call_args)))
      .HelperPull(DecompressLoop(algorithm, sender, receiver));
}

}  // namespace grpc_core
