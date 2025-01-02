/*
 *
 * Copyright 2015 gRPC authors.
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

#import <objc/runtime.h>

#import "GRPCCall+OAuth2.h"

static NSString *const kAuthorizationHeader = @"authorization";
static NSString *const kBearerPrefix = @"Bearer ";
static NSString *const kChallengeHeader = @"www-authenticate";

@implementation GRPCCall (OAuth2)
@dynamic tokenProvider;

- (NSString *)oauth2AccessToken {
  NSString *headerValue = self.requestHeaders[kAuthorizationHeader];
  if ([headerValue hasPrefix:kBearerPrefix]) {
    return [headerValue substringFromIndex:kBearerPrefix.length];
  } else {
    return nil;
  }
}

- (void)setOauth2AccessToken:(NSString *)token {
  if (token) {
    self.requestHeaders[kAuthorizationHeader] = [kBearerPrefix stringByAppendingString:token];
  } else {
    [self.requestHeaders removeObjectForKey:kAuthorizationHeader];
  }
}

- (NSString *)oauth2ChallengeHeader {
  return self.responseHeaders[kChallengeHeader];
}

- (void)setTokenProvider:(id<GRPCAuthorizationProtocol>)tokenProvider {
  objc_setAssociatedObject(self, @selector(tokenProvider), tokenProvider, OBJC_ASSOCIATION_RETAIN);
}

- (id<GRPCAuthorizationProtocol>)tokenProvider {
  return objc_getAssociatedObject(self, @selector(tokenProvider));
}

@end
