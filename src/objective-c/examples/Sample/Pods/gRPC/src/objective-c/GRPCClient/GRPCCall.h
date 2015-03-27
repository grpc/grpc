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
#import <RxLibrary/GRXWriter.h>

@class GRPCMethodName;

@class GRPCCall;

// The gRPC protocol is an RPC protocol on top of HTTP2.
//
// While the most common type of RPC receives only one request message and
// returns only one response message, the protocol also supports RPCs that
// return multiple individual messages in a streaming fashion, RPCs that
// accept a stream of request messages, or RPCs with both streaming requests
// and responses.
//
// Conceptually, each gRPC call consists of a bidirectional stream of binary
// messages, with RPCs of the "non-streaming type" sending only one message in
// the corresponding direction (the protocol doesn't make any distinction).
//
// Each RPC uses a different HTTP2 stream, and thus multiple simultaneous RPCs
// can be multiplexed transparently on the same TCP connection.
@interface GRPCCall : NSObject<GRXWriter>

// These HTTP2 headers will be passed to the server as part of this call. Each
// HTTP2 header is a name-value pair with string names and either string or binary values.
// The passed dictionary has to use NSString keys, corresponding to the header names. The
// value associated to each can be a NSString object or a NSData object. E.g.:
//
// call.requestMetadata = @{
//     @"Authorization": @"Bearer ...",
//     @"SomeBinaryHeader": someData
// };
//
// After the call is started, modifying this won't have any effect.
@property(nonatomic, readwrite) NSMutableDictionary *requestMetadata;

// This isn't populated until the first event is delivered to the handler.
@property(atomic, readonly) NSDictionary *responseMetadata;

// The request writer has to write NSData objects into the provided Writeable. The server will
// receive each of those separately and in order.
// A gRPC call might not complete until the request writer finishes. On the other hand, the
// request finishing doesn't necessarily make the call to finish, as the server might continue
// sending messages to the response side of the call indefinitely (depending on the semantics of
// the specific remote method called).
// To finish a call right away, invoke cancel.
- (instancetype)initWithHost:(NSString *)host
                      method:(GRPCMethodName *)method
              requestsWriter:(id<GRXWriter>)requestsWriter NS_DESIGNATED_INITIALIZER;

// Finishes the request side of this call, notifies the server that the RPC
// should be cancelled, and finishes the response side of the call with an error
// of code CANCELED.
- (void)cancel;

// TODO(jcanizales): Let specify a deadline. As a category of GRXWriter?
@end
