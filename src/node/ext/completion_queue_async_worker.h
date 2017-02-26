/*
 *
 * Copyright 2015, gRPC authors
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

#ifndef NET_GRPC_NODE_COMPLETION_QUEUE_ASYNC_WORKER_H_
#define NET_GRPC_NODE_COMPLETION_QUEUE_ASYNC_WORKER_H_
#include <nan.h>

#include "grpc/grpc.h"

namespace grpc {
namespace node {

/* A worker that asynchronously calls completion_queue_next, and queues onto the
   node event loop a call to the function stored in the event's tag. */
class CompletionQueueAsyncWorker : public Nan::AsyncWorker {
 public:
  CompletionQueueAsyncWorker();

  ~CompletionQueueAsyncWorker();
  /* Calls completion_queue_next with the provided deadline, and stores the
     event if there was one or sets an error message if there was not */
  void Execute();

  /* Returns the completion queue attached to this class */
  static grpc_completion_queue *GetQueue();

  /* Convenience function to create a worker with the given arguments and queue
     it to run asynchronously */
  static void Next();

  /* Initialize the CompletionQueueAsyncWorker class */
  static void Init(v8::Local<v8::Object> exports);

 protected:
  /* Called when Execute has succeeded (completed without setting an error
     message). Calls the saved callback with the event that came from
     completion_queue_next */
  void HandleOKCallback();

  void HandleErrorCallback();

 private:
  grpc_event result;

  static grpc_completion_queue *queue;

  // Number of grpc_completion_queue_next calls in the thread pool
  static int current_threads;
  // Number of grpc_completion_queue_next calls waiting to enter the thread pool
  static int waiting_next_calls;
};

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_COMPLETION_QUEUE_ASYNC_WORKER_H_
