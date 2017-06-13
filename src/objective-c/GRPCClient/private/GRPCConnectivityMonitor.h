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

#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>

@interface GRPCReachabilityFlags : NSObject

+ (nonnull instancetype)flagsWithFlags:(SCNetworkReachabilityFlags)flags;

/**
 * One accessor method to query each of the different flags. Example:

@property(nonatomic, readonly) BOOL isCell;

 */
#define GRPC_XMACRO_ITEM(methodName, FlagName) \
@property(nonatomic, readonly) BOOL methodName;

#include "GRPCReachabilityFlagNames.xmacro.h"
#undef GRPC_XMACRO_ITEM

@property(nonatomic, readonly) BOOL isHostReachable;
@end

@interface GRPCConnectivityMonitor : NSObject

+ (nullable instancetype)monitorWithHost:(nonnull NSString *)hostName;

- (nonnull instancetype)init NS_UNAVAILABLE;

/**
 * Queue on which callbacks will be dispatched. Default is the main queue. Set it before calling
 * handleLossWithHandler:.
 */
// TODO(jcanizales): Default to a serial background queue instead.
@property(nonatomic, strong, null_resettable) dispatch_queue_t queue;

/**
 * Calls handler every time the connectivity to this instance's host is lost. If this instance is
 * released before that happens, the handler won't be called.
 * Only one handler is active at a time, so if this method is called again before the previous
 * handler has been called, it might never be called at all (or yes, if it has already been queued).
 */
- (void)handleLossWithHandler:(nullable void (^)())lossHandler
      wifiStatusChangeHandler:(nullable void (^)())wifiStatusChangeHandler;
@end
