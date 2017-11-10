/*
 *
 * Copyright 2016 gRPC authors.
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

#include <string.h>

extern "C" {
#include "src/core/lib/channel/channel_stack.h"
}
#include "src/cpp/common/channel_filter.h"

#include <grpc++/impl/codegen/slice.h>

namespace grpc {

// MetadataBatch

grpc_linked_mdelem* MetadataBatch::AddMetadata(grpc_exec_ctx* exec_ctx,
                                               const string& key,
                                               const string& value) {
  grpc_linked_mdelem* storage = new grpc_linked_mdelem;
  memset(storage, 0, sizeof(grpc_linked_mdelem));
  storage->md = grpc_mdelem_from_slices(exec_ctx, SliceFromCopiedString(key),
                                        SliceFromCopiedString(value));
  GRPC_LOG_IF_ERROR("MetadataBatch::AddMetadata",
                    grpc_metadata_batch_link_head(exec_ctx, batch_, storage));
  return storage;
}

// ChannelData

void ChannelData::StartTransportOp(grpc_exec_ctx* exec_ctx,
                                   grpc_channel_element* elem,
                                   TransportOp* op) {
  grpc_channel_next_op(exec_ctx, elem, op->op());
}

void ChannelData::GetInfo(grpc_exec_ctx* exec_ctx, grpc_channel_element* elem,
                          const grpc_channel_info* channel_info) {
  grpc_channel_next_get_info(exec_ctx, elem, channel_info);
}

// CallData

void CallData::StartTransportStreamOpBatch(grpc_exec_ctx* exec_ctx,
                                           grpc_call_element* elem,
                                           TransportStreamOpBatch* op) {
  grpc_call_next_op(exec_ctx, elem, op->op());
}

void CallData::SetPollsetOrPollsetSet(grpc_exec_ctx* exec_ctx,
                                      grpc_call_element* elem,
                                      grpc_polling_entity* pollent) {
  grpc_call_stack_ignore_set_pollset_or_pollset_set(exec_ctx, elem, pollent);
}

// internal code used by RegisterChannelFilter()

namespace internal {

// Note: Implicitly initialized to nullptr due to static lifetime.
std::vector<FilterRecord>* channel_filters;

namespace {

bool MaybeAddFilter(grpc_exec_ctx* exec_ctx,
                    grpc_channel_stack_builder* builder, void* arg) {
  const FilterRecord& filter = *(FilterRecord*)arg;
  if (filter.include_filter) {
    const grpc_channel_args* args =
        grpc_channel_stack_builder_get_channel_arguments(builder);
    if (!filter.include_filter(*args)) return true;
  }
  return grpc_channel_stack_builder_prepend_filter(builder, &filter.filter,
                                                   nullptr, nullptr);
}

}  // namespace

void ChannelFilterPluginInit() {
  for (size_t i = 0; i < channel_filters->size(); ++i) {
    FilterRecord& filter = (*channel_filters)[i];
    grpc_channel_init_register_stage(filter.stack_type, filter.priority,
                                     MaybeAddFilter, (void*)&filter);
  }
}

void ChannelFilterPluginShutdown() {}

}  // namespace internal

}  // namespace grpc
