#import <Foundation/Foundation.h>

// TODO(jcanizales): Make the domain string public.
extern NSString *const kGRPCErrorDomain;

// TODO(jcanizales): Make this public and document each code.
typedef NS_ENUM(NSInteger, GRPCErrorCode) {
  GRPCErrorCodeCancelled = 1,
  GRPCErrorCodeUnknown = 2,
  GRPCErrorCodeInvalidArgument = 3,
  GRPCErrorCodeDeadlineExceeded = 4,
  GRPCErrorCodeNotFound = 5,
  GRPCErrorCodeAlreadyExists = 6,
  GRPCErrorCodePermissionDenied = 7,
  GRPCErrorCodeUnauthenticated = 16,
  GRPCErrorCodeResourceExhausted = 8,
  GRPCErrorCodeFailedPrecondition = 9,
  GRPCErrorCodeAborted = 10,
  GRPCErrorCodeOutOfRange = 11,
  GRPCErrorCodeUnimplemented = 12,
  GRPCErrorCodeInternal = 13,
  GRPCErrorCodeUnavailable = 14,
  GRPCErrorCodeDataLoss = 15
};

// TODO(jcanizales): This is conflating trailing metadata with Status details. Fix it once there's
// a decision on how to codify Status.
#include <grpc/status.h>
struct grpc_metadata;
struct grpc_status {
    grpc_status_code status;
    const char *details;
    size_t metadata_count;
    struct grpc_metadata *metadata_elements;
};

@interface NSError (GRPC)
// Returns nil if the status is OK. Otherwise, a NSError whose code is one of
// GRPCErrorCode and whose domain is kGRPCErrorDomain.
+ (instancetype)grpc_errorFromStatus:(struct grpc_status *)status;
@end
