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

#import <GRPCClient/GRPCInterceptor.h>

NS_ASSUME_NONNULL_BEGIN

@interface RequestCacheEntry : NSObject <NSCopying>

@property(readonly, copy, nullable) NSString *path;
@property(readonly, copy, nullable) id message;

@end

@interface MutableRequestCacheEntry : RequestCacheEntry

@property(copy, nullable) NSString *path;
@property(copy, nullable) id<NSObject> message;

@end

@interface ResponseCacheEntry : NSObject <NSCopying>

@property(readonly, copy, nullable) NSDate *deadline;

@property(readonly, copy, nullable) NSDictionary *headers;
@property(readonly, copy, nullable) id message;
@property(readonly, copy, nullable) NSDictionary *trailers;

@end

@interface MutableResponseCacheEntry : ResponseCacheEntry

@property(copy, nullable) NSDate *deadline;

@property(copy, nullable) NSDictionary *headers;
@property(copy, nullable) id message;
@property(copy, nullable) NSDictionary *trailers;

@end

@interface CacheContext : NSObject <GRPCInterceptorFactory>

- (nullable instancetype)init;

- (nullable ResponseCacheEntry *)getCachedResponseForRequest:(RequestCacheEntry *)request;

- (void)setCachedResponse:(ResponseCacheEntry *)response forRequest:(RequestCacheEntry *)request;

@end

@interface CacheInterceptor : GRPCInterceptor

- (instancetype)init NS_UNAVAILABLE;

+ (instancetype)new NS_UNAVAILABLE;

- (nullable instancetype)initWithInterceptorManager:
                             (GRPCInterceptorManager *_Nonnull)intercepterManager
                                       cacheContext:(CacheContext *_Nonnull)cacheContext
    NS_DESIGNATED_INITIALIZER;

// implementation of GRPCInterceptorInterface
- (void)startWithRequestOptions:(GRPCRequestOptions *)requestOptions
                    callOptions:(GRPCCallOptions *)callOptions;
- (void)writeData:(id)data;
- (void)finish;

// implementation of GRPCResponseHandler
- (void)didReceiveInitialMetadata:(nullable NSDictionary *)initialMetadata;
- (void)didReceiveData:(id)data;
- (void)didCloseWithTrailingMetadata:(nullable NSDictionary *)trailingMetadata
                               error:(nullable NSError *)error;

@end

NS_ASSUME_NONNULL_END
