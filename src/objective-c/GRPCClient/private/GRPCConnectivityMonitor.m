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

#import "GRPCConnectivityMonitor.h"

#include <netinet/in.h>

NSString *kGRPCConnectivityNotification = @"kGRPCConnectivityNotification";

static SCNetworkReachabilityRef reachability;
static GRPCConnectivityStatus currentStatus;

// Aggregate information in flags into network status.
GRPCConnectivityStatus CalculateConnectivityStatus(SCNetworkReachabilityFlags flags) {
  GRPCConnectivityStatus result = GRPCConnectivityUnknown;
  if (((flags & kSCNetworkReachabilityFlagsReachable) == 0) ||
      ((flags & kSCNetworkReachabilityFlagsConnectionRequired) != 0)) {
    return GRPCConnectivityNoNetwork;
  }
  result = GRPCConnectivityWiFi;
#if TARGET_OS_IPHONE
  if (flags & kSCNetworkReachabilityFlagsIsWWAN) {
    return result = GRPCConnectivityCellular;
  }
#endif
  return result;
}

static void ReachabilityCallback(
    SCNetworkReachabilityRef target, SCNetworkReachabilityFlags flags, void* info) {
  GRPCConnectivityStatus newStatus = CalculateConnectivityStatus(flags);

  if (newStatus != currentStatus) {
    [[NSNotificationCenter defaultCenter] postNotificationName:kGRPCConnectivityNotification
                                                        object:nil];
    currentStatus = newStatus;
  }
}

@implementation GRPCConnectivityMonitor

+ (void)initialize {
  if (self == [GRPCConnectivityMonitor self]) {
    struct sockaddr_in addr = {0};
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    reachability = SCNetworkReachabilityCreateWithAddress(NULL, (struct sockaddr *)&addr);
    currentStatus = GRPCConnectivityUnknown;

    SCNetworkConnectionFlags flags;
    if (SCNetworkReachabilityGetFlags(reachability, &flags)) {
      currentStatus = CalculateConnectivityStatus(flags);
    }

    SCNetworkReachabilityContext context = {0, (__bridge void *)(self), NULL, NULL, NULL};
    if (!SCNetworkReachabilitySetCallback(reachability, ReachabilityCallback, &context) ||
        !SCNetworkReachabilityScheduleWithRunLoop(
            reachability, CFRunLoopGetMain(), kCFRunLoopCommonModes)) {
      NSLog(@"gRPC connectivity monitor fail to set");
    }
  }
}

+ (void)registerObserver:(_Nonnull id)observer
                selector:(SEL)selector {
  [[NSNotificationCenter defaultCenter] addObserver:observer
                                           selector:selector
                                               name:kGRPCConnectivityNotification
                                             object:nil];
}

+ (void)unregisterObserver:(_Nonnull id)observer {
  [[NSNotificationCenter defaultCenter] removeObserver:observer];
}

@end
