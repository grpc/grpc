#import <Foundation/Foundation.h>

@protocol GRXWriteable;
@protocol GRXWriter;

// This is a thread-safe wrapper over a GRXWriteable instance. It lets one
// enqueue calls to a GRXWriteable instance for the main thread, guaranteeing
// that didFinishWithError: is the last message sent to it (no matter what
// messages are sent to the wrapper, in what order, nor from which thread). It
// also guarantees that, if cancelWithError: is called from the main thread
// (e.g. by the app cancelling the writes), no further messages are sent to the
// writeable except didFinishWithError:.
//
// TODO(jcanizales): Let the user specify another queue for the writeable
// callbacks.
// TODO(jcanizales): Rename to GRXWriteableWrapper and move to the Rx library.
@interface GRPCDelegateWrapper : NSObject

// The GRXWriteable passed is the wrapped writeable.
// Both the GRXWriter instance and the GRXWriteable instance are retained until
// didFinishWithError: is sent to the writeable, and released after that.
// This is used to create a retain cycle that keeps both objects alive until the
// writing is explicitly finished.
- (instancetype)initWithWriteable:(id<GRXWriteable>)writeable writer:(id<GRXWriter>)writer
    NS_DESIGNATED_INITIALIZER;

// Enqueues didReceiveValue: to be sent to the writeable in the main thread.
// The passed handler is invoked from the main thread after didReceiveValue:
// returns.
- (void)enqueueMessage:(NSData *)message completionHandler:(void (^)())handler;

// Enqueues didFinishWithError:nil to be sent to the writeable in the main
// thread. After that message is sent to the writeable, all other methods of
// this object are effectively noops.
- (void)enqueueSuccessfulCompletion;

// If the writeable has not yet received a didFinishWithError: message, this
// will enqueue one to be sent to it in the main thread, and cancel all other
// pending messages to the writeable enqueued by this object (both past and
// future).
// The error argument cannot be nil.
- (void)cancelWithError:(NSError *)error;

// Cancels all pending messages to the writeable enqueued by this object (both
// past and future). Because the writeable won't receive didFinishWithError:,
// this also releases the writeable and the writer.
- (void)cancelSilently;
@end
