#include <node.h>
#include <nan.h>

#include "grpc/grpc.h"
#include "grpc/support/time.h"
#include "completion_queue_async_worker.h"
#include "event.h"
#include "tag.h"

namespace grpc {
namespace node {

using v8::Function;
using v8::Handle;
using v8::Object;
using v8::Persistent;
using v8::Value;

grpc_completion_queue *CompletionQueueAsyncWorker::queue;

CompletionQueueAsyncWorker::CompletionQueueAsyncWorker() :
    NanAsyncWorker(NULL) {
}

CompletionQueueAsyncWorker::~CompletionQueueAsyncWorker() {
}

void CompletionQueueAsyncWorker::Execute() {
  result = grpc_completion_queue_next(queue, gpr_inf_future);
}

grpc_completion_queue *CompletionQueueAsyncWorker::GetQueue() {
  return queue;
}

void CompletionQueueAsyncWorker::Next() {
  NanScope();
  CompletionQueueAsyncWorker *worker = new CompletionQueueAsyncWorker();
  NanAsyncQueueWorker(worker);
}

void CompletionQueueAsyncWorker::Init(Handle<Object> exports) {
  NanScope();
  queue = grpc_completion_queue_create();
}

void CompletionQueueAsyncWorker::HandleOKCallback() {
  NanScope();
  NanCallback event_callback(GetTagHandle(result->tag).As<Function>());
  Handle<Value> argv[] = {
    CreateEventObject(result)
  };

  DestroyTag(result->tag);
  grpc_event_finish(result);
  result = NULL;

  event_callback.Call(1, argv);
}

}  // namespace node
}  // namespace grpc
