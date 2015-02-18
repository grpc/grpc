#import "GRPCMethodName.h"

@implementation GRPCMethodName
- (instancetype)initWithPackage:(NSString *)package
                      interface:(NSString *)interface
                         method:(NSString *)method {
  if ((self = [super init])) {
    _package = [package copy];
    _interface = [interface copy];
    _method = [method copy];
  }
  return self;
}
@end
