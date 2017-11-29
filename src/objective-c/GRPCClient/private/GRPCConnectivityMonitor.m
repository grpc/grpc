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

#pragma mark Flags

@implementation GRPCReachabilityFlags {
  SCNetworkReachabilityFlags _flags;
}

+ (instancetype)flagsWithFlags:(SCNetworkReachabilityFlags)flags {
  return [[self alloc] initWithFlags:flags];
}

- (instancetype)initWithFlags:(SCNetworkReachabilityFlags)flags {
  if ((self = [super init])) {
    _flags = flags;
  }
  return self;
}

/*
 * One accessor method implementation per flag. Example:

- (BOOL)isCell { \
  return !!(_flags & kSCNetworkReachabilityFlagsIsWWAN); \
}

 */
#define GRPC_XMACRO_ITEM(methodName, FlagName) \
- (BOOL)methodName { \
  return !!(_flags & kSCNetworkReachabilityFlags ## FlagName); \
}
#include "GRPCReachabilityFlagNames.xmacro.h"
#undef GRPC_XMACRO_ITEM

- (BOOL)isHostReachable {
  // Note: connectionOnDemand means it'll be reachable only if using the CFSocketStream API or APIs
  // on top of it.
  // connectionRequired means we can't tell until a connection is attempted (e.g. for VPN on
  // demand).
  return self.reachable && !self.interventionRequired && !self.connectionOnDemand;
}

- (NSString *)description {
  NSMutableArray *activeOptions = [NSMutableArray arrayWithCapacity:9];

  /*
   * For each flag, add its name to the array if it's ON. Example:

  if (self.isCell) {
    [activeOptions addObject:@"isCell"];
  }

   */
  #define GRPC_XMACRO_ITEM(methodName, FlagName) \
    if (self.methodName) {                       \
      [activeOptions addObject:@ #methodName];   \
    }
  #include "GRPCReachabilityFlagNames.xmacro.h"
  #undef GRPC_XMACRO_ITEM

  return activeOptions.count == 0 ? @"(none)" : [activeOptions componentsJoinedByString:@", "];
}

- (BOOL)isEqual:(id)object {
  return [object isKindOfClass:[GRPCReachabilityFlags class]] &&
      _flags == ((GRPCReachabilityFlags *)object)->_flags;
}

- (NSUInteger)hash {
  return _flags;
}
@end

#pragma mark Connectivity Monitor

// Assumes the third argument is a block that accepts a GRPCReachabilityFlags object, and passes the
// received ones to it.
static void PassFlagsToContextInfoBlock(SCNetworkReachabilityRef target,
                                        SCNetworkReachabilityFlags flags,
                                        void *info) {
  #pragma unused (target)
  // This can be called many times with the same info. The info is retained by SCNetworkReachability
  // while this function is being executed.
  void (^handler)(GRPCReachabilityFlags *) = (__bridge void (^)(GRPCReachabilityFlags *))info;
  handler([[GRPCReachabilityFlags alloc] initWithFlags:flags]);
}

@implementation GRPCConnectivityMonitor {
  SCNetworkReachabilityRef _reachabilityRef;
  GRPCReachabilityFlags *_previousReachabilityFlags;
}

- (nullable instancetype)initWithReachability:(nullable SCNetworkReachabilityRef)reachability {
  if (!reachability) {
    return nil;
  }
  if ((self = [super init])) {
    _reachabilityRef = CFRetain(reachability);
    _queue = dispatch_get_main_queue();
    _previousReachabilityFlags = nil;
  }
  return self;
}

+ (nullable instancetype)monitorWithHost:(nonnull NSString *)host {
  const char *hostName = host.UTF8String;
  if (!hostName) {
    [NSException raise:NSInvalidArgumentException
                format:@"host.UTF8String returns NULL for %@", host];
  }
  SCNetworkReachabilityRef reachability =
      SCNetworkReachabilityCreateWithName(NULL, hostName);

  GRPCConnectivityMonitor *returnValue = [[self alloc] initWithReachability:reachability];
  if (reachability) {
    CFRelease(reachability);
  }
  return returnValue;
}

- (void)handleLossWithHandler:(nullable void (^)(void))lossHandler
      wifiStatusChangeHandler:(nullable void (^)(void))wifiStatusChangeHandler {
  __weak typeof(self) weakSelf = self;
  [self startListeningWithHandler:^(GRPCReachabilityFlags *flags) {
    typeof(self) strongSelf = weakSelf;
    if (strongSelf) {
      if (lossHandler && !flags.reachable) {
        lossHandler();
#if TARGET_OS_IPHONE
      } else if (wifiStatusChangeHandler &&
                 strongSelf->_previousReachabilityFlags &&
                 (flags.isWWAN ^
                  strongSelf->_previousReachabilityFlags.isWWAN)) {
        wifiStatusChangeHandler();
#endif
      }
      strongSelf->_previousReachabilityFlags = flags;
    }
  }];
}

- (void)startListeningWithHandler:(void (^)(GRPCReachabilityFlags *))handler {
  // Copy to ensure the handler block is in the heap (and so can't be deallocated when this method
  // returns).
  void (^copiedHandler)(GRPCReachabilityFlags *) = [handler copy];
  SCNetworkReachabilityContext context = {
    .version = 0,
    .info = (__bridge void *)copiedHandler,
    .retain = CFRetain,
    .release = CFRelease,
  };
  // The following will retain context.info, and release it when the callback is set to NULL.
  SCNetworkReachabilitySetCallback(_reachabilityRef, PassFlagsToContextInfoBlock, &context);
  SCNetworkReachabilitySetDispatchQueue(_reachabilityRef, _queue);
}

- (void)stopListening {
  // This releases the block on context.info.
  SCNetworkReachabilitySetCallback(_reachabilityRef, NULL, NULL);
  SCNetworkReachabilitySetDispatchQueue(_reachabilityRef, NULL);
}

- (void)setQueue:(dispatch_queue_t)queue {
  _queue = queue ?: dispatch_get_main_queue();
}

- (void)dealloc {
  if (_reachabilityRef) {
    [self stopListening];
    CFRelease(_reachabilityRef);
  }
}

@end
