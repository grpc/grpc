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
@end
