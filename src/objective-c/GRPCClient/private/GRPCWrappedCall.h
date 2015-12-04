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

#import "GRPCChannel.h"
#import "GRPCRequestHeaders.h"

@interface GRPCOperation : NSObject
@property(nonatomic, readonly) grpc_op op;
/** Guaranteed to be called when the operation has finished. */
- (void)finish;
@end

@interface GRPCOpSendMetadata : GRPCOperation

- (instancetype)initWithMetadata:(NSDictionary *)metadata
                         handler:(void(^)())handler NS_DESIGNATED_INITIALIZER;

@end

@interface GRPCOpSendMessage : GRPCOperation

- (instancetype)initWithMessage:(NSData *)message
                        handler:(void(^)())handler NS_DESIGNATED_INITIALIZER;

@end

@interface GRPCOpSendClose : GRPCOperation

- (instancetype)initWithHandler:(void(^)())handler NS_DESIGNATED_INITIALIZER;

@end

@interface GRPCOpRecvMetadata : GRPCOperation

- (instancetype)initWithHandler:(void(^)(NSDictionary *))handler NS_DESIGNATED_INITIALIZER;

@end

@interface GRPCOpRecvMessage : GRPCOperation

- (instancetype)initWithHandler:(void(^)(grpc_byte_buffer *))handler NS_DESIGNATED_INITIALIZER;

@end

@interface GRPCOpRecvStatus : GRPCOperation

- (instancetype)initWithHandler:(void(^)(NSError *, NSDictionary *))handler
    NS_DESIGNATED_INITIALIZER;

@end

#pragma mark GRPCWrappedCall

@interface GRPCWrappedCall : NSObject

- (instancetype)initWithHost:(NSString *)host
                        path:(NSString *)path NS_DESIGNATED_INITIALIZER;

- (void)startBatchWithOperations:(NSArray *)ops errorHandler:(void(^)())errorHandler;

- (void)startBatchWithOperations:(NSArray *)ops;

- (void)cancel;
@end
