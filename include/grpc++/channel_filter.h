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

#include <functional>
#include <vector>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/surface/channel_init.h"

//
// An interface to define filters.
//
// To define a filter, implement a subclass of each of CallData and
// ChannelData.  Then register the filter using something like this:
//   RegisterChannelFilter<MyChannelDataSubclass, MyCallDataSubclass>(
//       "name-of-filter", GRPC_SERVER_CHANNEL, INT_MAX, nullptr);
//

namespace grpc {

// Represents call data.
// Note: Must be copyable.
class CallData {
 public:
  virtual ~CallData() {}

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
  virtual ~ChannelData() {}

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
 public:
  static const size_t call_data_size = sizeof(CallDataType);

  static void InitCallElement(
      grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
      grpc_call_element_args *args) {
    // Construct the object in the already-allocated memory.
    new (elem->call_data) CallDataType();
  }

  static void DestroyCallElement(
      grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
      const grpc_call_stats *stats, void *and_free_memory) {
    reinterpret_cast<CallDataType*>(elem->call_data)->~CallDataType();
  }

  static void StartTransportStreamOp(
      grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
      grpc_transport_stream_op *op) {
    CallDataType* call_data = (CallDataType*)elem->call_data;
    call_data->StartTransportStreamOp(exec_ctx, elem, op);
  }

  static void SetPollsetOrPollsetSet(
      grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
      grpc_polling_entity *pollent) {
    CallDataType* call_data = (CallDataType*)elem->call_data;
    call_data->SetPollsetOrPollsetSet(exec_ctx, elem, pollent);
  }

  static char* GetPeer(
      grpc_exec_ctx *exec_ctx, grpc_call_element *elem) {
    CallDataType* call_data = (CallDataType*)elem->call_data;
    return call_data->GetPeer(exec_ctx, elem);
  }

  static const size_t channel_data_size = sizeof(ChannelDataType);

  static void InitChannelElement(
      grpc_exec_ctx *exec_ctx, grpc_channel_element *elem,
      grpc_channel_element_args *args) {
    // Construct the object in the already-allocated memory.
    new (elem->channel_data) ChannelDataType();
  }

  static void DestroyChannelElement(
      grpc_exec_ctx *exec_ctx, grpc_channel_element *elem) {
    reinterpret_cast<ChannelDataType*>(elem->channel_data)->~ChannelDataType();
  }

  static void StartTransportOp(
      grpc_exec_ctx *exec_ctx, grpc_channel_element *elem,
      grpc_transport_op *op) {
    ChannelDataType* channel_data = (ChannelDataType*)elem->channel_data;
    channel_data->StartTransportOp(exec_ctx, elem, op);
  }
};

struct FilterRecord {
  grpc_channel_stack_type stack_type;
  int priority;
  std::function<bool(const grpc_channel_args*)> include_filter;
  grpc_channel_filter filter;
};
extern std::vector<FilterRecord>* channel_filters;

void ChannelFilterPluginInit();
void ChannelFilterPluginShutdown();

}  // namespace internal

// Registers a new filter.
// Must be called by only one thread at a time.
// The include_filter argument specifies a function that will be called
// to determine at run-time whether or not to add the filter.  If the
// value is nullptr, the filter will be added unconditionally.
template<typename ChannelDataType, typename CallDataType>
void RegisterChannelFilter(
    const char* name, grpc_channel_stack_type stack_type, int priority,
    std::function<bool(const grpc_channel_args*)> include_filter) {
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
  internal::FilterRecord filter_record = {
      stack_type, priority, include_filter, {
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
          name}};
  internal::channel_filters->push_back(filter_record);
}

}  // namespace grpc

#endif  // GRPCXX_CHANNEL_FILTER_H
