#import "GRPCMethodName+HTTP2Encoding.h"

@implementation GRPCMethodName (HTTP2Encoding)
- (NSString *)HTTP2Path {
  if (self.package) {
    return [NSString stringWithFormat:@"/%@.%@/%@", self.package, self.interface, self.method];
  } else {
    return [NSString stringWithFormat:@"/%@/%@", self.interface, self.method];
  }
}
@end
