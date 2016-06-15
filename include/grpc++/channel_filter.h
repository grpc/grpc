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

#ifndef GRPCXX_CHANNEL_FILTER_H
#define GRPCXX_CHANNEL_FILTER_H

#include <grpc/grpc.h>

#include <vector>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/surface/channel_init.h"

//
// An interface to define filters.
//
// To define a filter, implement a subclass of each of CallData and
// ChannelData.  Then register the filter like this:
//   RegisterChannelFilter<MyChannelDataSubclass, MyCallDataSubclass>(
//       "name-of-filter", GRPC_SERVER_CHANNEL, INT_MAX);
//

namespace grpc {

// Represents call data.
// Note: Must be copyable.
class CallData {
 public:
  // Do not override the destructor.  Instead, put clean-up code in the
  // Destroy() method.
  virtual ~CallData() {}

  virtual void Destroy() {}

  virtual void StartTransportStreamOp(
      grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
      grpc_transport_stream_op *op);

  virtual void SetPollsetOrPollsetSet(
      grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
      grpc_polling_entity *pollent);

  virtual char* GetPeer(grpc_exec_ctx *exec_ctx, grpc_call_element *elem);

 protected:
  CallData() {}
};

// Represents channel data.
// Note: Must be copyable.
class ChannelData {
 public:
  // Do not override the destructor.  Instead, put clean-up code in the
  // Destroy() method.
  virtual ~ChannelData() {}

  virtual void Destroy() {}

  virtual void StartTransportOp(
      grpc_exec_ctx *exec_ctx, grpc_channel_element *elem,
      grpc_transport_op *op);

 protected:
  ChannelData() {}
};

namespace internal {

// Defines static members for passing to C core.
template<typename ChannelDataType, typename CallDataType>
class ChannelFilter {
  static const size_t call_data_size = sizeof(CallDataType);

  static void InitCallElement(
      grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
      grpc_call_element_args *args) {
    CallDataType* call_data = elem->call_data;
    *call_data = CallDataType();
  }

  static void DestroyCallElement(
      grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
      const grpc_call_stats *stats, void *and_free_memory) {
    CallDataType* call_data = elem->call_data;
    // Can't destroy the object here, since it's not allocated by
    // itself; instead, it's part of a larger allocation managed by
    // C-core.  So instead, we call the Destroy() method.
    call_data->Destroy();
  }

  static void StartTransportStreamOp(
      grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
      grpc_transport_stream_op *op) {
    CallDataType* call_data = elem->call_data;
    call_data->StartTransportStreamOp(exec_ctx, op);
  }

  static void SetPollsetOrPollsetSet(
      grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
      grpc_polling_entity *pollent) {
    CallDataType* call_data = elem->call_data;
    call_data->SetPollsetOrPollsetSet(exec_ctx, pollent);
  }

  static char* GetPeer(
      grpc_exec_ctx *exec_ctx, grpc_call_element *elem) {
    CallDataType* call_data = elem->call_data;
    return call_data->GetPeer(exec_ctx);
  }

  static const size_t channel_data_size = sizeof(ChannelDataType);

  static void InitChannelElement(
      grpc_exec_ctx *exec_ctx, grpc_channel_element *elem,
      grpc_channel_element_args *args) {
    ChannelDataType* channel_data = elem->channel_data;
    *channel_data = ChannelDataType();
  }

  static void DestroyChannelElement(
      grpc_exec_ctx *exec_ctx, grpc_channel_element *elem) {
    ChannelDataType* channel_data = elem->channel_data;
    // Can't destroy the object here, since it's not allocated by
    // itself; instead, it's part of a larger allocation managed by
    // C-core.  So instead, we call the Destroy() method.
    channel_data->Destroy();
  }

  static void StartTransportOp(
      grpc_exec_ctx *exec_ctx, grpc_channel_element *elem,
      grpc_transport_op *op) {
    ChannelDataType* channel_data = elem->channel_data;
    channel_data->StartTransportOp(exec_ctx, op);
  }
};

struct FilterRecord {
  grpc_channel_stack_type stack_type;
  int priority;
  grpc_channel_filter filter;
};
extern std::vector<FilterRecord>* channel_filters;

void ChannelFilterPluginInit();
void ChannelFilterPluginShutdown() {}

}  // namespace internal

// Registers a new filter.
// Must be called by only one thread at a time.
template<typename ChannelDataType, typename CallDataType>
void RegisterChannelFilter(const char* name,
                           grpc_channel_stack_type stack_type, int priority) {
  // If we haven't been called before, initialize channel_filters and
  // call grpc_register_plugin().
  if (internal::channel_filters == nullptr) {
    grpc_register_plugin(internal::ChannelFilterPluginInit,
                         internal::ChannelFilterPluginShutdown);
    internal::channel_filters = new std::vector<internal::FilterRecord>();
  }
  // Add an entry to channel_filters.  The filter will be added when the
  // C-core initialization code calls ChannelFilterPluginInit().
  typedef internal::ChannelFilter<ChannelDataType, CallDataType> FilterType;
  internal::channel_filters->emplace_back({
      stack_type, priority, {
          FilterType::StartTransportStreamOp,
          FilterType::StartTransportOp,
          FilterType::call_data_size,
          FilterType::InitCallElement,
          FilterType::SetPollsetOrPollsetSet,
          FilterType::DestroyCallElement,
          FilterType::channel_data_size,
          FilterType::InitChannelElement,
          FilterType::DestroyChannelElement,
          FilterType::GetPeer,
          name}});
}

}  // namespace grpc

#endif  // GRPCXX_CHANNEL_FILTER_H
