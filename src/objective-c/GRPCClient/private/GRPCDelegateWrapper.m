#import "GRPCDelegateWrapper.h"

#import <RxLibrary/GRXWriteable.h>

@interface GRPCDelegateWrapper ()
// These are atomic so that cancellation can nillify them from any thread.
@property(atomic, strong) id<GRXWriteable> writeable;
@property(atomic, strong) id<GRXWriter> writer;
@end

@implementation GRPCDelegateWrapper {
  dispatch_queue_t _writeableQueue;
  // This ensures that didFinishWithError: is only sent once to the writeable.
  dispatch_once_t _alreadyFinished;
}

- (instancetype)init {
  return [self initWithWriteable:nil writer:nil];
}

// Designated initializer
- (instancetype)initWithWriteable:(id<GRXWriteable>)writeable writer:(id<GRXWriter>)writer {
  if (self = [super init]) {
    _writeableQueue = dispatch_get_main_queue();
    _writeable = writeable;
    _writer = writer;
  }
  return self;
}

- (void)enqueueMessage:(NSData *)message completionHandler:(void (^)())handler {
  dispatch_async(_writeableQueue, ^{
    // We're racing a possible cancellation performed by another thread. To turn
    // all already-enqueued messages into noops, cancellation nillifies the
    // writeable property. If we get it before it's nil, we won
    // the race.
    id<GRXWriteable> writeable = self.writeable;
    if (writeable) {
      [writeable didReceiveValue:message];
      handler();
    }
  });
}

- (void)enqueueSuccessfulCompletion {
  dispatch_async(_writeableQueue, ^{
    dispatch_once(&_alreadyFinished, ^{
      // Cancellation is now impossible. None of the other three blocks can run
      // concurrently with this one.
      [self.writeable didFinishWithError:nil];
      // Break the retain cycle with writer, and skip any possible message to the
      // wrapped writeable enqueued after this one.
      self.writeable = nil;
      self.writer = nil;
    });
  });
}

- (void)cancelWithError:(NSError *)error {
  NSAssert(error, @"For a successful completion, use enqueueSuccessfulCompletion.");
  dispatch_once(&_alreadyFinished, ^{
    // Skip any of the still-enqueued messages to the wrapped writeable. We use
    // the atomic setter to nillify writer and writeable because we might be
    // running concurrently with the blocks in _writeableQueue, and assignment
    // with ARC isn't atomic.
    id<GRXWriteable> writeable = self.writeable;
    self.writeable = nil;

    dispatch_async(_writeableQueue, ^{
      [writeable didFinishWithError:error];
      // Break the retain cycle with writer.
      self.writer = nil;
    });
  });
}

- (void)cancelSilently {
  dispatch_once(&_alreadyFinished, ^{
    // Skip any of the still-enqueued messages to the wrapped writeable. We use
    // the atomic setter to nillify writer and writeable because we might be
    // running concurrently with the blocks in _writeableQueue, and assignment
    // with ARC isn't atomic.
    self.writeable = nil;
    self.writer = nil;
  });
}
@end
