/*
 *
 * Copyright 2015, Google Inc.
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

#import "MakeRPCViewController.h"

#import <AuthTestService/AuthSample.pbrpc.h>
#import <Google/SignIn.h>
#import <ProtoRPC/ProtoRPC.h>

NSString * const kTestScope = @"https://www.googleapis.com/auth/xapi.zoo";

static NSString * const kTestHostAddress = @"grpc-test.sandbox.googleapis.com";

// Category for RPC errors to create the descriptions as we want them to appear on our view.
@interface NSError (AuthSample)
- (NSString *)UIDescription;
@end

@implementation NSError (AuthSample)
- (NSString *)UIDescription {
  if (self.code == GRPCErrorCodeUnauthenticated) {
    // Authentication error. OAuth2 specifies we'll receive a challenge header.
    // |userInfo[kGRPCHeadersKey]| is the dictionary of response headers.
    NSString *challengeHeader = self.userInfo[kGRPCHeadersKey][@"www-authenticate"] ?: @"";
    return [@"Invalid credentials. Server challenge:\n" stringByAppendingString:challengeHeader];
  } else {
    // Any other error.
    return [NSString stringWithFormat:@"Unexpected RPC error %li: %@",
            (long)self.code, self.localizedDescription];
  }
}
@end

@implementation MakeRPCViewController

- (void)viewWillAppear:(BOOL)animated {

  // Create a service client and a proto request as usual.
  AUTHTestService *client = [[AUTHTestService alloc] initWithHost:kTestHostAddress];

  AUTHRequest *request = [AUTHRequest message];
  request.fillUsername = YES;
  request.fillOauthScope = YES;

  // Create a not-yet-started RPC. We want to set the request headers on this object before starting
  // it.
  ProtoRPC *call =
      [client RPCToUnaryCallWithRequest:request handler:^(AUTHResponse *response, NSError *error) {
        if (response) {
          // This test server responds with the email and scope of the access token it receives.
          self.mainLabel.text = [NSString stringWithFormat:@"Used scope: %@ on behalf of user %@",
                                 response.oauthScope, response.username];

        } else {
          self.mainLabel.text = error.UIDescription;
        }
      }];

  // Set the access token to be used.
  NSString *accessToken = GIDSignIn.sharedInstance.currentUser.authentication.accessToken;
  call.requestHeaders[@"Authorization"] = [@"Bearer " stringByAppendingString:accessToken];

  // Start the RPC.
  [call start];

  self.mainLabel.text = @"Waiting for RPC to complete...";
}

@end
