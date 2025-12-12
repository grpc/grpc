/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#import "GRPCCompletionQueue.h"

#import <grpc/grpc.h>

const grpc_completion_queue_attributes kCompletionQueueAttr = {
    GRPC_CQ_CURRENT_VERSION, GRPC_CQ_NEXT, GRPC_CQ_DEFAULT_POLLING, NULL};

const char *kEnableCustomConcurrentCompletionQueue =
    "grpc_objc_enable_custom_concurrent_completion_queue";

@implementation GRPCCompletionQueue

+ (instancetype)completionQueue {
  static GRPCCompletionQueue *singleton = nil;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    singleton = [[self alloc] init];
  });
  return singleton;
}

- (instancetype)init {
  if ((self = [super init])) {
    _unmanagedQueue = grpc_completion_queue_create(
        grpc_completion_queue_factory_lookup(&kCompletionQueueAttr), &kCompletionQueueAttr, NULL);

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
    static dispatch_queue_t concurrentDispatchQueue;
    dispatch_once(&initialization, ^{
      char *useCustomQueue = getenv(kEnableCustomConcurrentCompletionQueue);
      if (useCustomQueue != nil && useCustomQueue[0] == '1') {
        dispatch_queue_attr_t attr = dispatch_queue_attr_make_with_qos_class(
            DISPATCH_QUEUE_CONCURRENT, QOS_CLASS_DEFAULT, 0);
        concurrentDispatchQueue = dispatch_queue_create("grpc.completionQueue", attr);
      } else {
        concurrentDispatchQueue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
      }
    });
    dispatch_async(concurrentDispatchQueue, ^{
      while (YES) {
        @autoreleasepool {
          // The following call blocks until an event is available.
          grpc_event event =
              grpc_completion_queue_next(unmanagedQueue, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
          GRPCQueueCompletionHandler handler;
          switch (event.type) {
            case GRPC_OP_COMPLETE:
              handler = (__bridge_transfer GRPCQueueCompletionHandler)event.tag;
              handler(event.success);
              break;
            case GRPC_QUEUE_SHUTDOWN:
              grpc_completion_queue_destroy(unmanagedQueue);
              return;
            case GRPC_QUEUE_TIMEOUT:
              NSLog(@"GRPC_QUEUE_TIMEOUT, success: %d, tag: %p", event.success, event.tag);
              break;
            default:
              [NSException raise:@"Unrecognized completion type" format:@""];
          }
        }
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
