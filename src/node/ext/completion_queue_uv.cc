/*
 *
 * Copyright 2016, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifdef GRPC_UV

#include <uv.h>
#include <node.h>
#include <v8.h>
#include <grpc/grpc.h>

#include "call.h"
#include "completion_queue.h"

namespace grpc {
namespace node {

using v8::Local;
using v8::Object;
using v8::Value;

grpc_completion_queue *queue;
uv_prepare_t prepare;
int pending_batches;

void drain_completion_queue(uv_prepare_t *handle) {
  Nan::HandleScope scope;
  grpc_event event;
  (void)handle;
  do {
    event = grpc_completion_queue_next(
        queue, gpr_inf_past(GPR_CLOCK_MONOTONIC), NULL);

    if (event.type == GRPC_OP_COMPLETE) {
      Nan::Callback *callback = grpc::node::GetTagCallback(event.tag);
      if (event.success) {
        Local<Value> argv[] = {Nan::Null(),
                             grpc::node::GetTagNodeValue(event.tag)};
        callback->Call(2, argv);
      } else {
        Local<Value> argv[] = {Nan::Error(
            "The async function encountered an error")};
        callback->Call(1, argv);
      }
      grpc::node::CompleteTag(event.tag);
      grpc::node::DestroyTag(event.tag);
      pending_batches--;
      if (pending_batches == 0) {
        uv_prepare_stop(&prepare);
      }
    }
  } while (event.type != GRPC_QUEUE_TIMEOUT);
}

grpc_completion_queue *GetCompletionQueue() {
  return queue;
}

void CompletionQueueNext() {
  if (pending_batches == 0) {
    GPR_ASSERT(!uv_is_active((uv_handle_t *)&prepare));
    uv_prepare_start(&prepare, drain_completion_queue);
  }
  pending_batches++;
}

void CompletionQueueInit(Local<Object> exports) {
  queue = grpc_completion_queue_create(NULL);
  uv_prepare_init(uv_default_loop(), &prepare);
  pending_batches = 0;
}

}  // namespace node
}  // namespace grpc

#endif /* GRPC_UV */
