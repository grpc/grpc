#ifndef NET_GRPC_NODE_COMPLETION_QUEUE_ASYNC_WORKER_H_
#define NET_GRPC_NODE_COMPLETION_QUEUE_ASYNC_WORKER_H_
#include <nan.h>

#include "grpc/grpc.h"

namespace grpc {
namespace node {

/* A worker that asynchronously calls completion_queue_next, and queues onto the
   node event loop a call to the function stored in the event's tag. */
class CompletionQueueAsyncWorker : public NanAsyncWorker {
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
  static void Init(v8::Handle<v8::Object> exports);

 protected:
  /* Called when Execute has succeeded (completed without setting an error
     message). Calls the saved callback with the event that came from
     completion_queue_next */
  void HandleOKCallback();

 private:
  grpc_event *result;

  static grpc_completion_queue *queue;
};

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_COMPLETION_QUEUE_ASYNC_WORKER_H_
