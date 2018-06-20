/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/cpp/ext/filters/census/client_filter.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "opencensus/stats/stats.h"
#include "src/core/lib/surface/call.h"
#include "src/cpp/ext/filters/census/grpc_plugin.h"
#include "src/cpp/ext/filters/census/measures.h"

namespace grpc {

constexpr uint32_t CensusClientCallData::kMaxTraceContextLen;
constexpr uint32_t CensusClientCallData::kMaxTagsLen;

namespace {

void FilterTrailingMetadata(grpc_metadata_batch* b, uint64_t* elapsed_time) {
  if (b->idx.named.grpc_server_stats_bin != nullptr) {
    ServerStatsDeserialize(
        reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(
            GRPC_MDVALUE(b->idx.named.grpc_server_stats_bin->md))),
        GRPC_SLICE_LENGTH(GRPC_MDVALUE(b->idx.named.grpc_server_stats_bin->md)),
        elapsed_time);
    grpc_metadata_batch_remove(b, b->idx.named.grpc_server_stats_bin);
  }
}

}  // namespace

void CensusClientCallData::StartTransportStreamOpBatch(
    grpc_call_element* elem, TransportStreamOpBatch* op) {
  if (op->send_initial_metadata() != nullptr) {
    census_context* ctxt = op->get_census_context();
    GenerateClientContext(
        qualified_method_, &context_,
        (ctxt == nullptr) ? nullptr : reinterpret_cast<CensusContext*>(ctxt));
    size_t tracing_len = TraceContextSerialize(context_.Context(), tracing_buf_,
                                               kMaxTraceContextLen);
    if (tracing_len > 0) {
      GRPC_LOG_IF_ERROR(
          "census grpc_filter",
          grpc_metadata_batch_add_tail(
              op->send_initial_metadata()->batch(), &tracing_bin_,
              grpc_mdelem_from_slices(
                  GRPC_MDSTR_GRPC_TRACE_BIN,
                  grpc_slice_from_copied_buffer(tracing_buf_, tracing_len))));
    }
    grpc_slice tags = grpc_empty_slice();
    // TODO: Add in tagging serialization.
    size_t encoded_tags_len = StatsContextSerialize(kMaxTagsLen, &tags);
    if (encoded_tags_len > 0) {
      GRPC_LOG_IF_ERROR(
          "census grpc_filter",
          grpc_metadata_batch_add_tail(
              op->send_initial_metadata()->batch(), &stats_bin_,
              grpc_mdelem_from_slices(GRPC_MDSTR_GRPC_TAGS_BIN, tags)));
    }
  }
  if (op->send_message() != nullptr) {
    ++sent_message_count_;
  }
  // Call next op.
  grpc_call_next_op(elem, op->op());
}

void CensusClientCallData::StartTransportStreamRecvOpBatch(
    grpc_call_element* elem, grpc_transport_stream_recv_op_batch* batch,
    grpc_error* error) {
  if (batch->recv_message) {
    if (*batch->payload->recv_message.recv_message != nullptr) {
      recv_message_count_++;
    }
  }
  if (batch->recv_trailing_metadata) {
    FilterTrailingMetadata(
        batch->payload->recv_trailing_metadata.recv_trailing_metadata,
        &elapsed_time_);
  }
  grpc_call_prev_filter_recv_op_batch(elem, batch, error);
}

grpc_error* CensusClientCallData::Init(grpc_call_element* elem,
                                       const grpc_call_element_args* args) {
  path_ = grpc_slice_ref_internal(args->path);
  start_time_ = absl::Now();
  method_ = GetMethod(&path_);
  qualified_method_ = absl::StrCat("Sent.", method_);
  return GRPC_ERROR_NONE;
}

void CensusClientCallData::Destroy(grpc_call_element* elem,
                                   const grpc_call_final_info* final_info,
                                   grpc_closure* then_call_closure) {
  const uint64_t request_size = GetOutgoingDataSize(final_info);
  const uint64_t response_size = GetIncomingDataSize(final_info);
  double latency_ms = absl::ToDoubleMilliseconds(absl::Now() - start_time_);
  ::opencensus::stats::Record(
      {{RpcClientSentBytesPerRpc(), static_cast<double>(request_size)},
       {RpcClientReceivedBytesPerRpc(), static_cast<double>(response_size)},
       {RpcClientRoundtripLatency(), latency_ms},
       {RpcClientServerLatency(),
        ToDoubleMilliseconds(absl::Nanoseconds(elapsed_time_))},
       {RpcClientSentMessagesPerRpc(), sent_message_count_},
       {RpcClientReceivedMessagesPerRpc(), recv_message_count_}},
      {{ClientMethodTagKey(), method_},
       {ClientStatusTagKey(), StatusCodeToString(final_info->final_status)}});
  grpc_slice_unref_internal(path_);
  context_.EndSpan();
}

}  // namespace grpc
