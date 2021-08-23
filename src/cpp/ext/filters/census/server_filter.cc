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

#include "src/cpp/ext/filters/census/server_filter.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "opencensus/stats/stats.h"
#include "src/core/lib/surface/call.h"
#include "src/cpp/ext/filters/census/grpc_plugin.h"
#include "src/cpp/ext/filters/census/measures.h"

namespace grpc {

constexpr uint32_t CensusServerCallData::kMaxServerStatsLen;

namespace {

// server metadata elements
struct ServerMetadataElements {
  grpc_slice path;
  grpc_slice tracing_slice;
  grpc_slice census_proto;
};

void FilterInitialMetadata(grpc_metadata_batch* b,
                           ServerMetadataElements* sml) {
  if (b->idx.named.path != nullptr) {
    sml->path = grpc_slice_ref_internal(GRPC_MDVALUE(b->idx.named.path->md));
  }
  if (b->idx.named.grpc_trace_bin != nullptr) {
    sml->tracing_slice =
        grpc_slice_ref_internal(GRPC_MDVALUE(b->idx.named.grpc_trace_bin->md));
    grpc_metadata_batch_remove(b, GRPC_BATCH_GRPC_TRACE_BIN);
  }
  if (b->idx.named.grpc_tags_bin != nullptr) {
    sml->census_proto =
        grpc_slice_ref_internal(GRPC_MDVALUE(b->idx.named.grpc_tags_bin->md));
    grpc_metadata_batch_remove(b, GRPC_BATCH_GRPC_TAGS_BIN);
  }
}

}  // namespace

void CensusServerCallData::OnDoneRecvMessageCb(void* user_data,
                                               grpc_error_handle error) {
  grpc_call_element* elem = reinterpret_cast<grpc_call_element*>(user_data);
  CensusServerCallData* calld =
      reinterpret_cast<CensusServerCallData*>(elem->call_data);
  CensusChannelData* channeld =
      reinterpret_cast<CensusChannelData*>(elem->channel_data);
  GPR_ASSERT(calld != nullptr);
  GPR_ASSERT(channeld != nullptr);
  // Stream messages are no longer valid after receiving trailing metadata.
  if ((*calld->recv_message_) != nullptr) {
    ++calld->recv_message_count_;
  }
  grpc_core::Closure::Run(DEBUG_LOCATION, calld->initial_on_done_recv_message_,
                          GRPC_ERROR_REF(error));
}

void CensusServerCallData::OnDoneRecvInitialMetadataCb(
    void* user_data, grpc_error_handle error) {
  grpc_call_element* elem = reinterpret_cast<grpc_call_element*>(user_data);
  CensusServerCallData* calld =
      reinterpret_cast<CensusServerCallData*>(elem->call_data);
  GPR_ASSERT(calld != nullptr);
  if (error == GRPC_ERROR_NONE) {
    grpc_metadata_batch* initial_metadata = calld->recv_initial_metadata_;
    GPR_ASSERT(initial_metadata != nullptr);
    ServerMetadataElements sml;
    sml.path = grpc_empty_slice();
    sml.tracing_slice = grpc_empty_slice();
    sml.census_proto = grpc_empty_slice();
    FilterInitialMetadata(initial_metadata, &sml);
    calld->path_ = grpc_slice_ref_internal(sml.path);
    calld->method_ = GetMethod(&calld->path_);
    calld->qualified_method_ = absl::StrCat("Recv.", calld->method_);
    const char* tracing_str =
        GRPC_SLICE_IS_EMPTY(sml.tracing_slice)
            ? ""
            : reinterpret_cast<const char*>(
                  GRPC_SLICE_START_PTR(sml.tracing_slice));
    size_t tracing_str_len = GRPC_SLICE_IS_EMPTY(sml.tracing_slice)
                                 ? 0
                                 : GRPC_SLICE_LENGTH(sml.tracing_slice);
    GenerateServerContext(absl::string_view(tracing_str, tracing_str_len),
                          calld->qualified_method_, &calld->context_);
    grpc_slice_unref_internal(sml.tracing_slice);
    grpc_slice_unref_internal(sml.census_proto);
    grpc_slice_unref_internal(sml.path);
    grpc_census_call_set_context(
        calld->gc_, reinterpret_cast<census_context*>(&calld->context_));
  }
  grpc_core::Closure::Run(DEBUG_LOCATION,
                          calld->initial_on_done_recv_initial_metadata_,
                          GRPC_ERROR_REF(error));
}

void CensusServerCallData::StartTransportStreamOpBatch(
    grpc_call_element* elem, TransportStreamOpBatch* op) {
  if (op->recv_initial_metadata() != nullptr) {
    // substitute our callback for the op callback
    recv_initial_metadata_ = op->recv_initial_metadata()->batch();
    initial_on_done_recv_initial_metadata_ = op->recv_initial_metadata_ready();
    op->set_recv_initial_metadata_ready(&on_done_recv_initial_metadata_);
  }
  if (op->send_message() != nullptr) {
    ++sent_message_count_;
  }
  if (op->recv_message() != nullptr) {
    recv_message_ = op->op()->payload->recv_message.recv_message;
    initial_on_done_recv_message_ =
        op->op()->payload->recv_message.recv_message_ready;
    op->op()->payload->recv_message.recv_message_ready = &on_done_recv_message_;
  }
  // We need to record the time when the trailing metadata was sent to mark the
  // completeness of the request.
  if (op->send_trailing_metadata() != nullptr) {
    elapsed_time_ = absl::Now() - start_time_;
    size_t len = ServerStatsSerialize(absl::ToInt64Nanoseconds(elapsed_time_),
                                      stats_buf_, kMaxServerStatsLen);
    if (len > 0) {
      GRPC_LOG_IF_ERROR(
          "census grpc_filter",
          grpc_metadata_batch_add_tail(
              op->send_trailing_metadata()->batch(), &census_bin_,
              grpc_mdelem_from_slices(
                  GRPC_MDSTR_GRPC_SERVER_STATS_BIN,
                  grpc_core::UnmanagedMemorySlice(stats_buf_, len)),
              GRPC_BATCH_GRPC_SERVER_STATS_BIN));
    }
  }
  // Call next op.
  grpc_call_next_op(elem, op->op());
}

grpc_error_handle CensusServerCallData::Init(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  start_time_ = absl::Now();
  gc_ =
      grpc_call_from_top_element(grpc_call_stack_element(args->call_stack, 0));
  GRPC_CLOSURE_INIT(&on_done_recv_initial_metadata_,
                    OnDoneRecvInitialMetadataCb, elem,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&on_done_recv_message_, OnDoneRecvMessageCb, elem,
                    grpc_schedule_on_exec_ctx);
  auth_context_ = grpc_call_auth_context(gc_);
  return GRPC_ERROR_NONE;
}

void CensusServerCallData::Destroy(grpc_call_element* /*elem*/,
                                   const grpc_call_final_info* final_info,
                                   grpc_closure* /*then_call_closure*/) {
  const uint64_t request_size = GetOutgoingDataSize(final_info);
  const uint64_t response_size = GetIncomingDataSize(final_info);
  double elapsed_time_ms = absl::ToDoubleMilliseconds(elapsed_time_);
  grpc_auth_context_release(auth_context_);
  ::opencensus::stats::Record(
      {{RpcServerSentBytesPerRpc(), static_cast<double>(response_size)},
       {RpcServerReceivedBytesPerRpc(), static_cast<double>(request_size)},
       {RpcServerServerLatency(), elapsed_time_ms},
       {RpcServerSentMessagesPerRpc(), sent_message_count_},
       {RpcServerReceivedMessagesPerRpc(), recv_message_count_}},
      {{ServerMethodTagKey(), method_},
       {ServerStatusTagKey(), StatusCodeToString(final_info->final_status)}});
  grpc_slice_unref_internal(path_);
  context_.EndSpan();
}

}  // namespace grpc
