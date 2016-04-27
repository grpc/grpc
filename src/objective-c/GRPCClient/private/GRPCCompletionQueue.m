/*
 *
 * Copyright 2015, Google Inc.
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

#import "GRPCCompletionQueue.h"

#import <grpc/grpc.h>

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
    _unmanagedQueue = grpc_completion_queue_create(NULL);

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
        grpc_event event = grpc_completion_queue_next(unmanagedQueue,
                                                      gpr_inf_future(GPR_CLOCK_REALTIME),
                                                      NULL);
        GRPCQueueCompletionHandler handler;
        switch (event.type) {
          case GRPC_OP_COMPLETE:
            handler = (__bridge_transfer GRPCQueueCompletionHandler)event.tag;
            handler(event.success);
            break;
          case GRPC_QUEUE_SHUTDOWN:
            grpc_completion_queue_destroy(unmanagedQueue);
            return;
          default:
            [NSException raise:@"Unrecognized completion type" format:@""];
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
