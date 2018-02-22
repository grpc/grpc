/*
 *
 * Copyright 2017 gRPC authors.
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

@interface GRPCCall (MobileLog)
// Set the object to be passed down along channel stack with channel arg
// GRPC_ARG_MOBILE_LOG_CONFIG. The setting may be used by custom channel
// filters for metrics logging.
+ (void)setLogConfig:(id)logConfig;

// Obtain the object to be passed down along channel stack with channel arg
// GRPC_ARG_MOBILE_LOG_CONFIG.
+ (id)logConfig;
@end
