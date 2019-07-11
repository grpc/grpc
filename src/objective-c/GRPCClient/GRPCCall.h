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

// The legacy header is included for backwards compatibility. Some V1 API users are still using
// GRPCCall by importing GRPCCall.h header so we need this import. However, if a user needs to
// exclude the core implementation, they may do so by defining GRPC_OBJC_NO_LEGACY_COMPATIBILITY.
#ifndef GRPC_OBJC_NO_LEGACY_COMPATIBILITY
#import "GRPCCallLegacy.h"
#endif

NS_ASSUME_NONNULL_BEGIN

#pragma mark gRPC errors

/** Domain of NSError objects produced by gRPC. */
extern NSString *const kGRPCErrorDomain;

/**
 * gRPC error codes.
 * Note that a few of these are never produced by the gRPC libraries, but are of general utility for
 * server applications to produce.
 */
typedef NS_ENUM(NSUInteger, GRPCErrorCode) {
  GRPCErrorCodeOk = 0,

  /** The operation was cancelled (typically by the caller). */
  GRPCErrorCodeCancelled = 1,

  /**
   * Unknown error. Errors raised by APIs that do not return enough error information may be
   * converted to this error.
   */
  GRPCErrorCodeUnknown = 2,

  /**
   * The client specified an invalid argument. Note that this differs from FAILED_PRECONDITION.
   * INVALID_ARGUMENT indicates arguments that are problematic regardless of the state of the
   * server (e.g., a malformed file name).
   */
  GRPCErrorCodeInvalidArgument = 3,

  /**
   * Deadline expired before operation could complete. For operations that change the state of the
   * server, this error may be returned even if the operation has completed successfully. For
   * example, a successful response from the server could have been delayed long enough for the
   * deadline to expire.
   */
  GRPCErrorCodeDeadlineExceeded = 4,

  /** Some requested entity (e.g., file or directory) was not found. */
  GRPCErrorCodeNotFound = 5,

  /** Some entity that we attempted to create (e.g., file or directory) already exists. */
  GRPCErrorCodeAlreadyExists = 6,

  /**
   * The caller does not have permission to execute the specified operation. PERMISSION_DENIED isn't
   * used for rejections caused by exhausting some resource (RESOURCE_EXHAUSTED is used instead for
   * those errors). PERMISSION_DENIED doesn't indicate a failure to identify the caller
   * (UNAUTHENTICATED is used instead for those errors).
   */
  GRPCErrorCodePermissionDenied = 7,

  /**
   * The request does not have valid authentication credentials for the operation (e.g. the caller's
   * identity can't be verified).
   */
  GRPCErrorCodeUnauthenticated = 16,

  /** Some resource has been exhausted, perhaps a per-user quota. */
  GRPCErrorCodeResourceExhausted = 8,

  /**
   * The RPC was rejected because the server is not in a state required for the procedure's
   * execution. For example, a directory to be deleted may be non-empty, etc.
   * The client should not retry until the server state has been explicitly fixed (e.g. by
   * performing another RPC). The details depend on the service being called, and should be found in
   * the NSError's userInfo.
   */
  GRPCErrorCodeFailedPrecondition = 9,

  /**
   * The RPC was aborted, typically due to a concurrency issue like sequencer check failures,
   * transaction aborts, etc. The client should retry at a higher-level (e.g., restarting a read-
   * modify-write sequence).
   */
  GRPCErrorCodeAborted = 10,

  /**
   * The RPC was attempted past the valid range. E.g., enumerating past the end of a list.
   * Unlike INVALID_ARGUMENT, this error indicates a problem that may be fixed if the system state
   * changes. For example, an RPC to get elements of a list will generate INVALID_ARGUMENT if asked
   * to return the element at a negative index, but it will generate OUT_OF_RANGE if asked to return
   * the element at an index past the current size of the list.
   */
  GRPCErrorCodeOutOfRange = 11,

  /** The procedure is not implemented or not supported/enabled in this server. */
  GRPCErrorCodeUnimplemented = 12,

  /**
   * Internal error. Means some invariant expected by the server application or the gRPC library has
   * been broken.
   */
  GRPCErrorCodeInternal = 13,

  /**
   * The server is currently unavailable. This is most likely a transient condition and may be
   * corrected by retrying with a backoff. Note that it is not always safe to retry
   * non-idempotent operations.
   */
  GRPCErrorCodeUnavailable = 14,

  /** Unrecoverable data loss or corruption. */
  GRPCErrorCodeDataLoss = 15,
};

/**
 * Keys used in |NSError|'s |userInfo| dictionary to store the response headers and trailers sent by
 * the server.
 */
extern NSString *const kGRPCHeadersKey;
extern NSString *const kGRPCTrailersKey;

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
