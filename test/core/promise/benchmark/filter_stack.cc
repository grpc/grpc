// Copyright 2021 gRPC authors.
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

#include "test/core/promise/benchmark/filter_stack.h"

namespace filter_stack {

ChannelStack* MakeChannel(Filter** filters, size_t num_filters) {
  size_t size = sizeof(ChannelStack) + num_filters * sizeof(ChannelElem);
  size_t call_size = sizeof(CallStack) + num_filters * sizeof(CallElem);
  for (size_t i = 0; i < num_filters; i++) {
    size += filters[i]->sizeof_channel_data;
    call_size += filters[i]->sizeof_call_data;
  }
  char* data = new char[size];
  ChannelStack* stk = reinterpret_cast<ChannelStack*>(data);
  new (data) ChannelStack{0, num_filters, call_size};
  data += sizeof(ChannelStack);
  char* user_data = data + num_filters * sizeof(ChannelElem);
  for (size_t i = 0; i < num_filters; i++) {
    new (data) ChannelElem{filters[i], user_data};
    filters[i]->init_channel_data(reinterpret_cast<ChannelElem*>(data));
    data += sizeof(ChannelElem);
    user_data += filters[i]->sizeof_channel_data;
  }
  printf("CALL STACK SIZE: %d\n", static_cast<int>(call_size));
  return stk;
}

void FreeChannel(ChannelStack* stk) {
  ChannelElem* elems = reinterpret_cast<ChannelElem*>(stk + 1);
  for (size_t i = 0; i < stk->num_elems; i++) {
    elems[i].filter->destroy_channel_data(&elems[i]);
  }
  stk->~ChannelStack();
  delete[] reinterpret_cast<char*>(stk);
}

CallStack* MakeCall(ChannelStack* stk) {
  char* data = new char[stk->call_stack_size];
  CallStack* call = reinterpret_cast<CallStack*>(data);
  new (data) CallStack{{0}, stk->num_elems, {}};
  data += sizeof(CallStack);
  ChannelElem* channel_elems = reinterpret_cast<ChannelElem*>(stk + 1);
  char* user_data = data + stk->num_elems * sizeof(CallElem);
  for (size_t i = 0; i < stk->num_elems; i++) {
    new (data) CallElem{channel_elems[i].filter, channel_elems[i].channel_data,
                        user_data};
    channel_elems[i].filter->init_call_data(reinterpret_cast<CallElem*>(data));
    data += sizeof(CallElem);
    user_data += channel_elems[i].filter->sizeof_call_data;
  }
  return call;
}

void FreeCall(CallStack* stk) {
  if (stk->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    CallElem* elems = reinterpret_cast<CallElem*>(stk + 1);
    for (size_t i = 0; i < stk->num_elems; i++) {
      elems[i].filter->destroy_call_data(&elems[i]);
    }
    stk->~CallStack();
    delete[] reinterpret_cast<char*>(stk);
  }
}

void NoChannelData(ChannelElem*) {}
void NoCallData(CallElem*) {}

static void StartOp(CallElem* elem, Op* op) {
  elem->filter->start_transport_stream_op_batch(elem, op);
}

void CallNextOp(CallElem* elem, Op* op) { StartOp(elem + 1, op); }

void RunOp(CallStack* stk, Op* op) {
  absl::MutexLock lock(&stk->mutex);
  StartOp(reinterpret_cast<CallElem*>(stk + 1), op);
}

}  // namespace filter_stack
