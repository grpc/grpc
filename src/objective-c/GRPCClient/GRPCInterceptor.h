/*
 *
 * Copyright 2019 gRPC authors.
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
 * API for interceptors implementation. This feature is currently EXPERIMENTAL and is subject to
 * breaking changes without prior notice.
 */

#import "GRPCCall.h"

NS_ASSUME_NONNULL_BEGIN

@class GRPCInterceptorManager;
@class GRPCInterceptor;

@protocol GRPCInterceptorInterface<NSObject>

/** The queue on which all methods of this interceptor should be dispatched on */
@property(readonly) dispatch_queue_t requestDispatchQueue;

- (void)startWithRequestOptions:(GRPCRequestOptions *)requestOptions
                    callOptions:(GRPCCallOptions *)callOptions;

- (void)writeData:(id)data;

- (void)finish;

- (void)cancel;

- (void)receiveNextMessages:(NSUInteger)numberOfMessages;

@end

@protocol GRPCInterceptorFactory

/**
 * Create an interceptor object. gRPC uses the returned object as the interceptor for the current
 * call
 */
- (GRPCInterceptor *)createInterceptorWithManager:(GRPCInterceptorManager *)interceptorManager;

@end

@interface GRPCInterceptorManager : NSObject

- (instancetype)init NS_UNAVAILABLE;

+ (instancetype)new NS_UNAVAILABLE;

- (nullable instancetype)initWithNextInerceptor:(id<GRPCInterceptorInterface>)nextInterceptor NS_DESIGNATED_INITIALIZER;

/** Set the previous interceptor in the chain. Can only be set once. */
- (void)setPreviousInterceptor:(id<GRPCResponseHandler>)previousInterceptor;

/** Indicate shutdown of the interceptor; release the reference to other interceptors */
- (void)shutDown;

// Methods to forward GRPCInterceptorInterface calls to the next interceptor

/** Notify the next interceptor in the chain to start the call and pass arguments */
- (void)startNextInterceptorWithRequest:(GRPCRequestOptions *)requestOptions
                            callOptions:(GRPCCallOptions *)callOptions;

/** Pass a message to be sent to the next interceptor in the chain */
- (void)writeNextInterceptorWithData:(id)data;

/** Notify the next interceptor in the chain to finish the call */
- (void)finishNextInterceptor;

/** Notify the next interceptor in the chain to cancel the call */
- (void)cancelNextInterceptor;

/** Notify the next interceptor in the chain to receive more messages */
- (void)receiveNextInterceptorMessages:(NSUInteger)numberOfMessages;

// Methods to forward GRPCResponseHandler callbacks to the previous object

/** Forward initial metadata to the previous interceptor in the chain */
- (void)forwardPreviousInterceptorWithInitialMetadata:(nullable NSDictionary *)initialMetadata;

/** Forward a received message to the previous interceptor in the chain */
- (void)forwardPreviousIntercetporWithData:(nullable id)data;

/** Forward call close and trailing metadata to the previous interceptor in the chain */
- (void)forwardPreviousInterceptorCloseWithTrailingMetadata:
(nullable NSDictionary *)trailingMetadata
                                                      error:(nullable NSError *)error;

/** Forward write completion to the previous interceptor in the chain */
- (void)forwardPreviousInterceptorDidWriteData;

@end

/**
 * Base class for a gRPC interceptor. The implementation of the base class provides default behavior
 * of an interceptor, which is simply forward a request/callback to the next/previous interceptor in
 * the chain. The base class implementation uses the same dispatch queue for both requests and
 * callbacks.
 *
 * An interceptor implementation should inherit from this base class and initialize the base class
 * with [super initWithInterceptorManager:dispatchQueue:] for the default implementation to function
 * properly.
 */
@interface GRPCInterceptor : NSObject<GRPCInterceptorInterface, GRPCResponseHandler>

- (instancetype)init NS_UNAVAILABLE;

+ (instancetype)new NS_UNAVAILABLE;

/**
 * Initialize the interceptor with the next interceptor in the chain, and provide the dispatch queue
 * that this interceptor's methods are dispatched onto.
 */
- (nullable instancetype)initWithInterceptorManager:(GRPCInterceptorManager *)interceptorManager
                               requestDispatchQueue:(dispatch_queue_t)requestDispatchQueue
                              responseDispatchQueue:(dispatch_queue_t)responseDispatchQueue NS_DESIGNATED_INITIALIZER;

// Default implementation of GRPCInterceptorInterface

- (void)startWithRequestOptions:(GRPCRequestOptions *)requestOptions
                    callOptions:(GRPCCallOptions *)callOptions;
- (void)writeData:(id)data;
- (void)finish;
- (void)cancel;
- (void)receiveNextMessages:(NSUInteger)numberOfMessages;

// Default implementation of GRPCResponeHandler

- (void)didReceiveInitialMetadata:(nullable NSDictionary *)initialMetadata;
- (void)didReceiveData:(id)data;
- (void)didCloseWithTrailingMetadata:(nullable NSDictionary *)trailingMetadata
                               error:(nullable NSError *)error;
- (void)didWriteData;

@end

NS_ASSUME_NONNULL_END
