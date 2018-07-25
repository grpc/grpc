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
#import <RxLibrary/GRXWriter.h>

#include <AvailabilityMacros.h>

#pragma mark gRPC errors

/** Domain of NSError objects produced by gRPC. */
extern NSString *const kGRPCErrorDomain;

/**
 * gRPC error codes.
 * Note that a few of these are never produced by the gRPC libraries, but are of general utility for
 * server applications to produce.
 */
typedef NS_ENUM(NSUInteger, GRPCErrorCode) {
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
   * corrected by retrying with a backoff.
   */
  GRPCErrorCodeUnavailable = 14,

  /** Unrecoverable data loss or corruption. */
  GRPCErrorCodeDataLoss = 15,
};

/**
 * Safety remark of a gRPC method as defined in RFC 2616 Section 9.1
 */
typedef NS_ENUM(NSUInteger, GRPCCallSafety) {
  /** Signal that there is no guarantees on how the call affects the server state. */
  GRPCCallSafetyDefault = 0,
  /** Signal that the call is idempotent. gRPC is free to use PUT verb. */
  GRPCCallSafetyIdempotentRequest = 1,
  /** Signal that the call is cacheable and will not affect server state. gRPC is free to use GET
     verb. */
  GRPCCallSafetyCacheableRequest = 2,
};

/**
 * Keys used in |NSError|'s |userInfo| dictionary to store the response headers and trailers sent by
 * the server.
 */
extern id const kGRPCHeadersKey;
extern id const kGRPCTrailersKey;

#pragma mark GRPCCall

/** Represents a single gRPC remote call. */
@interface GRPCCall : GRXWriter

/**
 * The authority for the RPC. If nil, the default authority will be used. This property must be nil
 * when Cronet transport is enabled.
 */
@property(atomic, copy, readwrite) NSString *serverName;

/**
 * The timeout for the RPC call in seconds. If set to 0, the call will not timeout. If set to
 * positive, the gRPC call returns with status GRPCErrorCodeDeadlineExceeded if it is not completed
 * within \a timeout seconds. A negative value is not allowed.
 */
@property NSTimeInterval timeout;

/**
 * The container of the request headers of an RPC conforms to this protocol, which is a subset of
 * NSMutableDictionary's interface. It will become a NSMutableDictionary later on.
 * The keys of this container are the header names, which per the HTTP standard are case-
 * insensitive. They are stored in lowercase (which is how HTTP/2 mandates them on the wire), and
 * can only consist of ASCII characters.
 * A header value is a NSString object (with only ASCII characters), unless the header name has the
 * suffix "-bin", in which case the value has to be a NSData object.
 */
/**
 * These HTTP headers will be passed to the server as part of this call. Each HTTP header is a
 * name-value pair with string names and either string or binary values.
 *
 * The passed dictionary has to use NSString keys, corresponding to the header names. The value
 * associated to each can be a NSString object or a NSData object. E.g.:
 *
 * call.requestHeaders = @{@"authorization": @"Bearer ..."};
 *
 * call.requestHeaders[@"my-header-bin"] = someData;
 *
 * After the call is started, trying to modify this property is an error.
 *
 * The property is initialized to an empty NSMutableDictionary.
 */
@property(atomic, readonly) NSMutableDictionary *requestHeaders;

/**
 * This dictionary is populated with the HTTP headers received from the server. This happens before
 * any response message is received from the server. It has the same structure as the request
 * headers dictionary: Keys are NSString header names; names ending with the suffix "-bin" have a
 * NSData value; the others have a NSString value.
 *
 * The value of this property is nil until all response headers are received, and will change before
 * any of -writeValue: or -writesFinishedWithError: are sent to the writeable.
 */
@property(atomic, readonly) NSDictionary *responseHeaders;

/**
 * Same as responseHeaders, but populated with the HTTP trailers received from the server before the
 * call finishes.
 *
 * The value of this property is nil until all response trailers are received, and will change
 * before -writesFinishedWithError: is sent to the writeable.
 */
@property(atomic, readonly) NSDictionary *responseTrailers;

/**
 * The request writer has to write NSData objects into the provided Writeable. The server will
 * receive each of those separately and in order as distinct messages.
 * A gRPC call might not complete until the request writer finishes. On the other hand, the request
 * finishing doesn't necessarily make the call to finish, as the server might continue sending
 * messages to the response side of the call indefinitely (depending on the semantics of the
 * specific remote method called).
 * To finish a call right away, invoke cancel.
 * host parameter should not contain the scheme (http:// or https://), only the name or IP addr
 * and the port number, for example @"localhost:5050".
 */
- (instancetype)initWithHost:(NSString *)host
                        path:(NSString *)path
              requestsWriter:(GRXWriter *)requestsWriter NS_DESIGNATED_INITIALIZER;

/**
 * Finishes the request side of this call, notifies the server that the RPC should be cancelled, and
 * finishes the response side of the call with an error of code CANCELED.
 */
- (void)cancel;

/**
 * Set the call flag for a specific host path.
 *
 * Host parameter should not contain the scheme (http:// or https://), only the name or IP addr
 * and the port number, for example @"localhost:5050".
 */
+ (void)setCallSafety:(GRPCCallSafety)callSafety host:(NSString *)host path:(NSString *)path;

/**
 * Set the dispatch queue to be used for callbacks.
 *
 * This configuration is only effective before the call starts.
 */
- (void)setResponseDispatchQueue:(dispatch_queue_t)queue;

// TODO(jcanizales): Let specify a deadline. As a category of GRXWriter?
@end

#pragma mark Backwards compatibiity

/** This protocol is kept for backwards compatibility with existing code. */
DEPRECATED_MSG_ATTRIBUTE("Use NSDictionary or NSMutableDictionary instead.")
@protocol GRPCRequestHeaders<NSObject>
@property(nonatomic, readonly) NSUInteger count;

- (id)objectForKeyedSubscript:(id)key;
- (void)setObject:(id)obj forKeyedSubscript:(id)key;

- (void)removeAllObjects;
- (void)removeObjectForKey:(id)key;
@end

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated"
/** This is only needed for backwards-compatibility. */
@interface NSMutableDictionary (GRPCRequestHeaders)<GRPCRequestHeaders>
@end
#pragma clang diagnostic pop
