/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#import <Foundation/Foundation.h>
#include <grpc/grpc.h>

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

@interface NSError (GRPC)
// Returns nil if the status code is OK. Otherwise, a NSError whose code is one of |GRPCErrorCode|
// and whose domain is |kGRPCErrorDomain|.
+ (instancetype)grpc_errorFromStatusCode:(grpc_status_code)statusCode details:(char *)details;
@end
