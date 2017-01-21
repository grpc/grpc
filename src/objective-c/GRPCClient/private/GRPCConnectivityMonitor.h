/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
