#import <Foundation/Foundation.h>

// See the README file for an introduction to this library.

// A fully-qualified gRPC method name. Full qualification is needed because a gRPC endpoint can
// implement multiple interfaces.
// TODO(jcanizales): Is this proto-specific, or actual part of gRPC? If the former, move one layer up.
@interface GRPCMethodName : NSObject
@property(nonatomic, readonly) NSString *package;
@property(nonatomic, readonly) NSString *interface;
@property(nonatomic, readonly) NSString *method;
- (instancetype)initWithPackage:(NSString *)package
                      interface:(NSString *)interface
                         method:(NSString *)method;
@end
