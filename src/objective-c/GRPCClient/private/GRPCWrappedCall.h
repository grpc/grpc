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

typedef void(^GRPCCompletionHandler)(NSDictionary *);

@protocol GRPCOp <NSObject>

- (void)getOp:(grpc_op *)op;

- (void)finish;

@end

@interface GRPCOpSendMetadata : NSObject <GRPCOp>

- (instancetype)initWithMetadata:(NSDictionary *)metadata
                         handler:(void(^)(void))handler NS_DESIGNATED_INITIALIZER;

@end

@interface GRPCOpSendMessage : NSObject <GRPCOp>

- (instancetype)initWithMessage:(NSData *)message
                        handler:(void(^)(void))handler NS_DESIGNATED_INITIALIZER;

@end

@interface GRPCOpSendClose : NSObject <GRPCOp>

- (instancetype)initWithHandler:(void(^)(void))handler NS_DESIGNATED_INITIALIZER;

@end

@interface GRPCOpRecvMetadata : NSObject <GRPCOp>

- (instancetype)initWithHandler:(void(^)(NSDictionary *))handler NS_DESIGNATED_INITIALIZER;

@end

@interface GRPCOpRecvMessage : NSObject <GRPCOp>

- (instancetype)initWithHandler:(void(^)(grpc_byte_buffer *))handler NS_DESIGNATED_INITIALIZER;

@end

@interface GRPCOpRecvStatus : NSObject <GRPCOp>

- (instancetype)initWithHandler:(void(^)(NSError *, NSDictionary *))handler NS_DESIGNATED_INITIALIZER;

@end

@interface GRPCWrappedCall : NSObject

- (instancetype)initWithChannel:(GRPCChannel *)channel
                         method:(NSString *)method
                           host:(NSString *)host NS_DESIGNATED_INITIALIZER;

- (void)startBatchWithOperations:(NSArray *)ops errorHandler:(void(^)())errorHandler;

- (void)startBatchWithOperations:(NSArray *)ops;

- (void)cancel;
@end
