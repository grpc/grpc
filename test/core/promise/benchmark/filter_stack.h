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

#ifndef FILTER_STACK_H
#define FILTER_STACK_H

#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

namespace filter_stack {

struct Filter;

struct ChannelStack {
  uint64_t refcount;
  size_t num_elems;
  size_t call_stack_size;
};

struct CallStack {
  std::atomic<size_t> refcount;
  size_t num_elems;
  absl::Mutex mutex;
};

struct ChannelElem {
  Filter* filter;
  void* channel_data;
};

struct CallElem {
  Filter* filter;
  void* channel_data;
  void* call_data;
};

struct Closure {
  void* p;
  void (*f)(void* p, absl::Status);
  void Run(absl::Status status) { f(p, std::move(status)); }
};

struct Op {
  struct Payload {};

  Op()
      : send_initial_metadata(false),
        send_trailing_metadata(false),
        send_message(false),
        recv_initial_metadata(false),
        recv_message(false),
        recv_trailing_metadata(false),
        cancel_stream(false),
        is_traced(false) {}

  Op(const Op&) = delete;
  Op& operator=(const Op&) = delete;

  Payload* payload = nullptr;

  Closure* on_complete = nullptr;

  bool send_initial_metadata : 1;
  bool send_trailing_metadata : 1;
  bool send_message : 1;
  bool recv_initial_metadata : 1;
  bool recv_message : 1;
  bool recv_trailing_metadata : 1;
  bool cancel_stream : 1;
  bool is_traced : 1;
};

struct Filter {
  void (*start_transport_stream_op_batch)(CallElem* elem, Op* op);
  void (*init_call_data)(CallElem* elem);
  void (*destroy_call_data)(CallElem* elem);
  void (*init_channel_data)(ChannelElem* elem);
  void (*destroy_channel_data)(ChannelElem* elem);
  size_t sizeof_call_data;
  size_t sizeof_channel_data;
};

ChannelStack* MakeChannel(Filter** filters, size_t num_filters);
void FreeChannel(ChannelStack* stk);
CallStack* MakeCall(ChannelStack* stk);
void FreeCall(CallStack* stk);

void NoChannelData(ChannelElem*);
void NoCallData(CallElem*);

void CallNextOp(CallElem* elem, Op* op);

void RunOp(CallStack* stk, Op* op);

}  // namespace filter_stack

#endif
