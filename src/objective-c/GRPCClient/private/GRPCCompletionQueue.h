#import <Foundation/Foundation.h>

struct grpc_completion_queue;
struct grpc_event;

typedef void(^GRPCEventHandler)(struct grpc_event *event);

// This class lets one more easily use grpc_completion_queue. To use it, pass
// the value of the unmanagedQueue property of an instance of this class to
// grpc_call_start_invoke. Then for every grpc_call_* method that accepts a tag,
// you can pass a block of type GRPCEventHandler (remembering to cast it using
// __bridge_retained). The block is guaranteed to eventually be called, by a
// concurrent queue, and then released. Each such block is passed a pointer to
// the grpc_event that carried it (in event->tag).
// Release the GRPCCompletionQueue object only after you are not going to pass
// any more blocks to the grpc_call that's using it.
@interface GRPCCompletionQueue : NSObject
@property(nonatomic, readonly) struct grpc_completion_queue *unmanagedQueue;

+ (instancetype)completionQueue;
@end
