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

#import <Foundation/Foundation.h>
#import <GRPCClient/GRPCCall.h>

#import "ProtoMethod.h"

NS_ASSUME_NONNULL_BEGIN

@class GPBMessage;

/** An object can implement this protocol to receive responses from server from a call. */
@protocol GRPCProtoResponseHandler<NSObject>

@required

/**
 * All the responses must be issued to a user-provided dispatch queue. This property specifies the
 * dispatch queue to be used for issuing the notifications.
 */
@property(atomic, readonly) dispatch_queue_t dispatchQueue;

@optional

/**
 * Issued when initial metadata is received from the server.
 */
- (void)didReceiveInitialMetadata:(nullable NSDictionary *)initialMetadata;

/**
 * Issued when a message is received from the server. The message is the deserialized proto object.
 */
- (void)didReceiveProtoMessage:(nullable GPBMessage *)message;

/**
 * Issued when a call finished. If the call finished successfully, \a error is nil and \a
 * trainingMetadata consists any trailing metadata received from the server. Otherwise, \a error
 * is non-nil and contains the corresponding error information, including gRPC error codes and
 * error descriptions.
 */
- (void)didCloseWithTrailingMetadata:(nullable NSDictionary *)trailingMetadata
                               error:(nullable NSError *)error;

@end

/** A unary-request RPC call with Protobuf. */
@interface GRPCUnaryProtoCall : NSObject

- (instancetype)init NS_UNAVAILABLE;

+ (instancetype) new NS_UNAVAILABLE;

/**
 * Users should not use this initializer directly. Call objects will be created, initialized, and
 * returned to users by methods of the generated service.
 */
- (nullable instancetype)initWithRequestOptions:(GRPCRequestOptions *)requestOptions
                                        message:(GPBMessage *)message
                                responseHandler:(id<GRPCProtoResponseHandler>)handler
                                    callOptions:(nullable GRPCCallOptions *)callOptions
                                  responseClass:(Class)responseClass NS_DESIGNATED_INITIALIZER;

/**
 * Start the call. This function must only be called once for each instance.
 */
- (void)start;

/**
 * Cancel the request of this call at best effort. It attempts to notify the server that the RPC
 * should be cancelled, and issue didCloseWithTrailingMetadata:error: callback with error code
 * CANCELED if no other error code has already been issued.
 */
- (void)cancel;

@end

/** A client-streaming RPC call with Protobuf. */
@interface GRPCStreamingProtoCall : NSObject

- (instancetype)init NS_UNAVAILABLE;

+ (instancetype) new NS_UNAVAILABLE;

/**
 * Users should not use this initializer directly. Call objects will be created, initialized, and
 * returned to users by methods of the generated service.
 */
- (nullable instancetype)initWithRequestOptions:(GRPCRequestOptions *)requestOptions
                                responseHandler:(id<GRPCProtoResponseHandler>)handler
                                    callOptions:(nullable GRPCCallOptions *)callOptions
                                  responseClass:(Class)responseClass NS_DESIGNATED_INITIALIZER;

/**
 * Start the call. This function must only be called once for each instance.
 */
- (void)start;

/**
 * Cancel the request of this call at best effort. It attempts to notify the server that the RPC
 * should be cancelled, and issue didCloseWithTrailingMetadata:error: callback with error code
 * CANCELED if no other error code has already been issued.
 */
- (void)cancel;

/**
 * Send a message to the server. The message should be a Protobuf message which will be serialized
 * internally.
 */
- (void)writeMessage:(GPBMessage *)message;

/**
 * Finish the RPC request and half-close the call. The server may still send messages and/or
 * trailers to the client.
 */
- (void)finish;

@end

NS_ASSUME_NONNULL_END

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"

__attribute__((deprecated("Please use GRPCProtoCall."))) @interface ProtoRPC
    : GRPCCall

      /**
       * host parameter should not contain the scheme (http:// or https://), only the name or IP
       * addr and the port number, for example @"localhost:5050".
       */
      -
      (instancetype)initWithHost : (NSString *)host method
    : (GRPCProtoMethod *)method requestsWriter : (GRXWriter *)requestsWriter responseClass
    : (Class)responseClass responsesWriteable
    : (id<GRXWriteable>)responsesWriteable NS_DESIGNATED_INITIALIZER;

- (void)start;
@end

/**
 * This subclass is empty now. Eventually we'll remove ProtoRPC class
 * to avoid potential naming conflict
 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    @interface GRPCProtoCall : ProtoRPC
#pragma clang diagnostic pop

                               @end

#pragma clang diagnostic pop
