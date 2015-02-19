#import "GRPCCompletionQueue.h"

#import <grpc.h>

@implementation GRPCCompletionQueue

+ (instancetype)completionQueue {
  // TODO(jcanizales): Reuse completion queues to consume only one thread,
  // instead of one per call.
  return [[self alloc] init];
}

- (instancetype)init {
  if ((self = [super init])) {
    _unmanagedQueue = grpc_completion_queue_create();

    // This is for the following block to capture the pointer by value (instead
    // of retaining self and doing self->_unmanagedQueue). This is essential
    // because the block doesn't end until after grpc_completion_queue_shutdown
    // is called, and we only want that to happen after nobody's using the queue
    // anymore (i.e. on self dealloc). So the block would never end if it
    // retained self.
    grpc_completion_queue *unmanagedQueue = _unmanagedQueue;

    // Start a loop on a concurrent queue to read events from the completion
    // queue and dispatch each.
    static dispatch_once_t initialization;
    static dispatch_queue_t gDefaultConcurrentQueue;
    dispatch_once(&initialization, ^{
      gDefaultConcurrentQueue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
    });
    dispatch_async(gDefaultConcurrentQueue, ^{
      while (YES) {
        // The following call blocks until an event is available.
        grpc_event *event = grpc_completion_queue_next(unmanagedQueue, gpr_inf_future);
        switch (event->type) {
          case GRPC_WRITE_ACCEPTED:
          case GRPC_FINISH_ACCEPTED:
          case GRPC_CLIENT_METADATA_READ:
          case GRPC_READ:
          case GRPC_FINISHED:
            if (event->tag) {
              GRPCEventHandler handler = (__bridge_transfer GRPCEventHandler) event->tag;
              handler(event);
            }
            grpc_event_finish(event);
            continue;
          case GRPC_QUEUE_SHUTDOWN:
            grpc_completion_queue_destroy(unmanagedQueue);
            grpc_event_finish(event);
            return;
          case GRPC_SERVER_RPC_NEW:
            NSAssert(NO, @"C gRPC library produced a server-only event.");
            continue;
        }
        // This means the C gRPC library produced an event that wasn't known
        // when this library was written. To preserve evolvability, ignore the
        // unknown event on release builds.
        NSAssert(NO, @"C gRPC library produced an unknown event.");
      };
    });
  }
  return self;
}

- (void)dealloc {
  // This makes the completion queue produce a GRPC_QUEUE_SHUTDOWN event *after*
  // all other pending events are flushed. What this means is all the blocks
  // passed to the gRPC C library as void* are eventually called, even if some
  // are called after self is dealloc'd.
  grpc_completion_queue_shutdown(_unmanagedQueue);
}
@end
