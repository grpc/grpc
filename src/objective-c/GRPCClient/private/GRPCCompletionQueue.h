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

typedef void(^GRPCQueueCompletionHandler)(bool success);

/**
 * This class lets one more easily use |grpc_completion_queue|. To use it, pass the value of the
 * |unmanagedQueue| property of an instance of this class to |grpc_channel_create_call|. Then for
 * every |grpc_call_*| method that accepts a tag, you can pass a block of type
 * |GRPCQueueCompletionHandler| (remembering to cast it using |__bridge_retained|). The block is
 * guaranteed to eventually be called, by a concurrent queue, and then released. Each such block is
 * passed a |bool| that tells if the operation was successful.
 *
 * Release the GRPCCompletionQueue object only after you are not going to pass any more blocks to
 * the |grpc_call| that's using it.
 */
@interface GRPCCompletionQueue : NSObject
@property(nonatomic, readonly) grpc_completion_queue *unmanagedQueue;

+ (instancetype)completionQueue;
@end
