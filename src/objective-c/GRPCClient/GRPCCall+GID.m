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

#import "GRPCCall+GID.h"

static NSMutableArray *completionHandlers = nil;

@implementation GIDSignIn (GRPC)

- (void)getTokenWithHandler:(void (^)(NSString *token))handler {
  NSString *token = self.currentUser.authentication.accessToken;
  if (token == nil) {
    BOOL signIn = NO;
    @synchronized (self) {
      if (!completionHandlers) {
        completionHandlers = [NSMutableArray array];
      }
      [completionHandlers addObject:handler];
      if (1 == [completionHandlers count]) {
        signIn = YES;
      }
    }
    if (signIn) {
      [self signIn];
    }
  } else {
    handler(token);
  }
}

- (void)completeRPCHandlers {
  NSString *token = self.currentUser.authentication.accessToken;
  @synchronized (self) {
    for (void (^handler)(NSString*) in completionHandlers) {
      handler(token);
    }
    [completionHandlers removeAllObjects];
  }
}

@end
