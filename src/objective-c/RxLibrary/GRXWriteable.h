#import <Foundation/Foundation.h>

// A GRXWriteable is an object to which a sequence of values can be sent. The
// sequence finishes with an optional error.
@protocol GRXWriteable <NSObject>

// Push the next value of the sequence to the receiving object.
// TODO(jcanizales): Name it enumerator:(id<GRXEnumerator>) didProduceValue:(id)?
- (void)didReceiveValue:(id)value;

// Signal that the sequence is completed, or that an error ocurred. After this
// message is sent to the instance, neither it nor didReceiveValue: may be
// called again.
// TODO(jcanizales): enumerator:(id<GRXEnumerator>) didFinishWithError:(NSError*)?
- (void)didFinishWithError:(NSError *)errorOrNil;
@end

typedef void (^GRXValueHandler)(id value);
typedef void (^GRXCompletionHandler)(NSError *errorOrNil);

// Utility to create objects that conform to the GRXWriteable protocol, from
// blocks that handle each of the two methods of the protocol.
@interface GRXWriteable : NSObject<GRXWriteable>
- (instancetype)initWithValueHandler:(GRXValueHandler)valueHandler
                   completionHandler:(GRXCompletionHandler)completionHandler
    NS_DESIGNATED_INITIALIZER;
@end
