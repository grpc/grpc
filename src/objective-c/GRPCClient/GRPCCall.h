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

/**
 * The gRPC protocol is an RPC protocol on top of HTTP2.
 *
 * While the most common type of RPC receives only one request message and returns only one response
 * message, the protocol also supports RPCs that return multiple individual messages in a streaming
 * fashion, RPCs that accept a stream of request messages, or RPCs with both streaming requests and
 * responses.
 *
 * Conceptually, each gRPC call consists of a bidirectional stream of binary messages, with RPCs of
 * the "non-streaming type" sending only one message in the corresponding direction (the protocol
 * doesn't make any distinction).
 *
 * Each RPC uses a different HTTP2 stream, and thus multiple simultaneous RPCs can be multiplexed
 * transparently on the same TCP connection.
 */

#import <Foundation/Foundation.h>

#import "GRPCCallOptions.h"
#import "GRPCDispatchable.h"
#import "GRPCTypes.h"

// The legacy header is included for backwards compatibility. Some V1 API users are still using
// GRPCCall by importing GRPCCall.h header so we need this import.
#import "GRPCCallLegacy.h"

NS_ASSUME_NONNULL_BEGIN

/** An object can implement this protocol to receive responses from server from a call. */
@protocol GRPCResponseHandler<NSObject, GRPCDispatchable>

@optional

/**
 * Issued when initial metadata is received from the server.
 */
- (void)didReceiveInitialMetadata:(nullable NSDictionary *)initialMetadata;

/**
 * This method is deprecated and does not work with interceptors. To use GRPCCall2 interface with
 * interceptor, implement didReceiveData: instead. To implement an interceptor, please leave this
 * method unimplemented and implement didReceiveData: method instead. If this method and
 * didReceiveRawMessage are implemented at the same time, implementation of this method will be
 * ignored.
 *
 * Issued when a message is received from the server. The message is the raw data received from the
 * server, with decompression and without proto deserialization.
 */
- (void)didReceiveRawMessage:(nullable NSData *)message;

/**
 * Issued when a decompressed message is received from the server. The message is decompressed, and
 * deserialized if a marshaller is provided to the call (marshaller is work in progress).
 */
- (void)didReceiveData:(id)data;

/**
 * Issued when a call finished. If the call finished successfully, \a error is nil and \a
 * trainingMetadata consists any trailing metadata received from the server. Otherwise, \a error
 * is non-nil and contains the corresponding error information, including gRPC error codes and
 * error descriptions.
 */
- (void)didCloseWithTrailingMetadata:(nullable NSDictionary *)trailingMetadata
                               error:(nullable NSError *)error;

/**
 * Issued when flow control is enabled for the call and a message written with writeData: method of
 * GRPCCall2 is passed to gRPC core with SEND_MESSAGE operation.
 */
- (void)didWriteData;

@end

/**
 * Call related parameters. These parameters are automatically specified by Protobuf. If directly
 * using the \a GRPCCall2 class, users should specify these parameters manually.
 */
@interface GRPCRequestOptions : NSObject<NSCopying>

- (instancetype)init NS_UNAVAILABLE;

+ (instancetype) new NS_UNAVAILABLE;

/** Initialize with all properties. */
- (instancetype)initWithHost:(NSString *)host
                        path:(NSString *)path
                      safety:(GRPCCallSafety)safety NS_DESIGNATED_INITIALIZER;

/** The host serving the RPC service. */
@property(copy, readonly) NSString *host;
/** The path to the RPC call. */
@property(copy, readonly) NSString *path;
/**
 * Specify whether the call is idempotent or cachable. gRPC may select different HTTP verbs for the
 * call based on this information. The default verb used by gRPC is POST.
 */
@property(readonly) GRPCCallSafety safety;

@end

#pragma mark GRPCCall

/**
 * A \a GRPCCall2 object represents an RPC call.
 */
@interface GRPCCall2 : NSObject

- (instancetype)init NS_UNAVAILABLE;

+ (instancetype) new NS_UNAVAILABLE;

/**
 * Designated initializer for a call.
 * \param requestOptions Protobuf generated parameters for the call.
 * \param responseHandler The object to which responses should be issued.
 * \param callOptions Options for the call.
 */
- (instancetype)initWithRequestOptions:(GRPCRequestOptions *)requestOptions
                       responseHandler:(id<GRPCResponseHandler>)responseHandler
                           callOptions:(nullable GRPCCallOptions *)callOptions
    NS_DESIGNATED_INITIALIZER;
/**
 * Convenience initializer for a call that uses default call options (see GRPCCallOptions.m for
 * the default options).
 */
- (instancetype)initWithRequestOptions:(GRPCRequestOptions *)requestOptions
                       responseHandler:(id<GRPCResponseHandler>)responseHandler;

/**
 * Starts the call. This function must only be called once for each instance.
 */
- (void)start;

/**
 * Cancel the request of this call at best effort. It attempts to notify the server that the RPC
 * should be cancelled, and issue didCloseWithTrailingMetadata:error: callback with error code
 * CANCELED if no other error code has already been issued.
 */
- (void)cancel;

/**
 * Send a message to the server. The data is subject to marshaller serialization and compression
 * (marshaller is work in progress).
 */
- (void)writeData:(id)data;

/**
 * Finish the RPC request and half-close the call. The server may still send messages and/or
 * trailers to the client. The method must only be called once and after start is called.
 */
- (void)finish;

/**
 * Tell gRPC to receive the next N gRPC message from gRPC core.
 *
 * This method should only be used when flow control is enabled. When flow control is not enabled,
 * this method is a no-op.
 */
- (void)receiveNextMessages:(NSUInteger)numberOfMessages;

/**
 * Get a copy of the original call options.
 */
@property(readonly, copy) GRPCCallOptions *callOptions;

/** Get a copy of the original request options. */
@property(readonly, copy) GRPCRequestOptions *requestOptions;

@end

NS_ASSUME_NONNULL_END
