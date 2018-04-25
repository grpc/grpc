/*
 *
 * Copyright 2016 gRPC authors.
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
#import "GRPCCall.h"

#include <AvailabilityMacros.h>

typedef NS_ENUM(NSInteger, GRPCCompressAlgorithm) {
  GRPCCompressNone,
  GRPCCompressDeflate,
  GRPCCompressGzip,
};

/**
 * Methods to configure GRPC channel options.
 */
@interface GRPCCall (ChannelArg)

/**
 * Use the provided @c userAgentPrefix at the beginning of the HTTP User Agent string for all calls
 * to the specified @c host.
 */
+ (void)setUserAgentPrefix:(nonnull NSString *)userAgentPrefix forHost:(nonnull NSString *)host;

/** The default response size limit is 4MB. Set this to override that default. */
+ (void)setResponseSizeLimit:(NSUInteger)limit forHost:(nonnull NSString *)host;

+ (void)closeOpenConnections DEPRECATED_MSG_ATTRIBUTE("The API for this feature is experimental, "
                                                      "and might be removed or modified at any "
                                                      "time.");

+ (void)setDefaultCompressMethod:(GRPCCompressAlgorithm)algorithm
                         forhost:(nonnull NSString *)host;

/** Enable keepalive and configure keepalive parameters. A user should call this function once to
 * enable keepalive for a particular host. gRPC client sends a ping after every \a interval ms to
 * check if the transport is still alive. After waiting for \a timeout ms, if the client does not
 * receive the ping ack, it closes the transport; all pending calls to this host will fail with
 * error GRPC_STATUS_INTERNAL with error information "keepalive watchdog timeout". */
+ (void)setKeepaliveWithInterval:(int)interval
                         timeout:(int)timeout
                         forHost:(nonnull NSString *)host;

@end
