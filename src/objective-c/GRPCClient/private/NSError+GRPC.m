#import "NSError+GRPC.h"

#include <grpc.h>

NSString *const kGRPCErrorDomain = @"org.grpc";

@implementation NSError (GRPC)
+ (instancetype)grpc_errorFromStatus:(struct grpc_status *)status {
  if (status->status == GRPC_STATUS_OK) {
    return nil;
  }
  NSString *message =
      [NSString stringWithFormat:@"Code=%i Message='%s'", status->status, status->details];
  return [NSError errorWithDomain:kGRPCErrorDomain
                             code:status->status
                         userInfo:@{NSLocalizedDescriptionKey: message}];
}
@end
