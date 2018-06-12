/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#import "NSError+GRPC.h"

#include <grpc/grpc.h>

NSString *const kGRPCErrorDomain = @"io.grpc";

@implementation NSError (GRPC)
+ (instancetype)grpc_errorFromStatusCode:(grpc_status_code)statusCode details:(char *)details {
  if (statusCode == GRPC_STATUS_OK) {
    return nil;
  }
  NSString *message = [NSString stringWithCString:details encoding:NSUTF8StringEncoding];
  return [NSError errorWithDomain:kGRPCErrorDomain
                             code:statusCode
                         userInfo:@{NSLocalizedDescriptionKey : message}];
}
@end
