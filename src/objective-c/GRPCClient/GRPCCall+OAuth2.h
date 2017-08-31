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

#import "GRPCCall.h"

/**
 * The protocol of an OAuth2 token object from which GRPCCall can acquire a token.
 */
@protocol GRPCAuthorizationProtocol
- (void)getTokenWithHandler:(void (^)(NSString *token))hander;
@end

/** Helpers for setting and reading headers compatible with OAuth2. */
@interface GRPCCall (OAuth2)

/**
 * Setting this property is equivalent to setting "Bearer <passed token>" as the value of the
 * request header with key "authorization" (the authorization header). Setting it to nil removes the
 * authorization header from the request.
 * The value obtained by getting the property is the OAuth2 bearer token if the authorization header
 * of the request has the form "Bearer <token>", or nil otherwise.
 */
@property(atomic, copy) NSString *oauth2AccessToken;

/** Returns the value (if any) of the "www-authenticate" response header (the challenge header). */
@property(atomic, readonly) NSString *oauth2ChallengeHeader;

/**
 * The authorization token object to be used when starting the call. If the value is set to nil, no
 * oauth authentication will be used.
 *
 * If tokenProvider exists, it takes precedence over the token set by oauth2AccessToken.
 */
@property(atomic, strong) id<GRPCAuthorizationProtocol> tokenProvider;

@end
