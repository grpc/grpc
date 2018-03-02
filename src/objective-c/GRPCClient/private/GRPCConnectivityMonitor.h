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

typedef NS_ENUM(NSInteger, GRPCConnectivityStatus) {
  GRPCConnectivityUnknown = 0,
  GRPCConnectivityNoNetwork = 1,
  GRPCConnectivityCellular = 2,
  GRPCConnectivityWiFi = 3,
};

extern NSString * _Nonnull kGRPCConnectivityNotification;

// This interface monitors OS reachability interface for any network status
// change. Parties interested in these events should register themselves as
// observer.
@interface GRPCConnectivityMonitor : NSObject

- (nonnull instancetype)init NS_UNAVAILABLE;

// Register an object as observer of network status change. \a observer
// must have a notification method with one parameter of type
// (NSNotification *) and should pass it to parameter \a selector. The
// parameter of this notification method is not used for now.
+ (void)registerObserver:(_Nonnull id)observer
                selector:(_Nonnull SEL)selector;

// Ungegister an object from observers of network status change.
+ (void)unregisterObserver:(_Nonnull id)observer;

@end
