/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <string.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/cpp/common/channel_filter.h"

#include <grpc++/impl/codegen/slice.h>

namespace grpc {

// MetadataBatch

grpc_linked_mdelem *MetadataBatch::AddMetadata(grpc_exec_ctx *exec_ctx,
                                               const string &key,
                                               const string &value) {
  grpc_linked_mdelem *storage = new grpc_linked_mdelem;
  memset(storage, 0, sizeof(grpc_linked_mdelem));
  storage->md = grpc_mdelem_from_slices(exec_ctx, SliceFromCopiedString(key),
                                        SliceFromCopiedString(value));
  GRPC_LOG_IF_ERROR("MetadataBatch::AddMetadata",
                    grpc_metadata_batch_link_head(exec_ctx, batch_, storage));
  return storage;
}

// ChannelData

void ChannelData::StartTransportOp(grpc_exec_ctx *exec_ctx,
                                   grpc_channel_element *elem,
                                   TransportOp *op) {
  grpc_channel_next_op(exec_ctx, elem, op->op());
}

void ChannelData::GetInfo(grpc_exec_ctx *exec_ctx, grpc_channel_element *elem,
                          const grpc_channel_info *channel_info) {
  grpc_channel_next_get_info(exec_ctx, elem, channel_info);
}

// CallData

void CallData::StartTransportStreamOpBatch(grpc_exec_ctx *exec_ctx,
                                           grpc_call_element *elem,
                                           TransportStreamOpBatch *op) {
  grpc_call_next_op(exec_ctx, elem, op->op());
}

void CallData::SetPollsetOrPollsetSet(grpc_exec_ctx *exec_ctx,
                                      grpc_call_element *elem,
                                      grpc_polling_entity *pollent) {
  grpc_call_stack_ignore_set_pollset_or_pollset_set(exec_ctx, elem, pollent);
}

char *CallData::GetPeer(grpc_exec_ctx *exec_ctx, grpc_call_element *elem) {
  return grpc_call_next_get_peer(exec_ctx, elem);
}

// internal code used by RegisterChannelFilter()

namespace internal {

// Note: Implicitly initialized to nullptr due to static lifetime.
std::vector<FilterRecord> *channel_filters;

namespace {

bool MaybeAddFilter(grpc_exec_ctx *exec_ctx,
                    grpc_channel_stack_builder *builder, void *arg) {
  const FilterRecord &filter = *(FilterRecord *)arg;
  if (filter.include_filter) {
    const grpc_channel_args *args =
        grpc_channel_stack_builder_get_channel_arguments(builder);
    if (!filter.include_filter(*args)) return true;
  }
  return grpc_channel_stack_builder_prepend_filter(builder, &filter.filter,
                                                   nullptr, nullptr);
}

}  // namespace

void ChannelFilterPluginInit() {
  for (size_t i = 0; i < channel_filters->size(); ++i) {
    FilterRecord &filter = (*channel_filters)[i];
    grpc_channel_init_register_stage(filter.stack_type, filter.priority,
                                     MaybeAddFilter, (void *)&filter);
  }
}

void ChannelFilterPluginShutdown() {}

}  // namespace internal

}  // namespace grpc
