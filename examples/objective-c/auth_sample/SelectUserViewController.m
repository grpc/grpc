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

#import "SelectUserViewController.h"

#import "MakeRPCViewController.h"

@implementation SelectUserViewController

- (void)viewDidLoad {
  [super viewDidLoad];

  self.signOutButton.layer.cornerRadius = 5;
  self.signOutButton.hidden = YES;

  // As instructed in https://developers.google.com/identity/sign-in/ios/sign-in
  GIDSignIn *signIn = GIDSignIn.sharedInstance;
  signIn.delegate = self;
  signIn.uiDelegate = self;

  // As instructed in https://developers.google.com/identity/sign-in/ios/additional-scopes
  if (![signIn.scopes containsObject:kTestScope]) {
    signIn.scopes = [signIn.scopes arrayByAddingObject:kTestScope];
  }

  [signIn signInSilently];
}

- (void)signIn:(GIDSignIn *)signIn
didSignInForUser:(GIDGoogleUser *)user
     withError:(NSError *)error {
  if (error) {
    // The user probably cancelled the sign-in flow.
    return;
  }

  self.mainLabel.text = [NSString stringWithFormat:@"User: %@", user.profile.email];
  NSString *scopes = [user.accessibleScopes componentsJoinedByString:@", "];
  scopes = scopes.length ? scopes : @"(none)";
  self.subLabel.text = [NSString stringWithFormat:@"Scopes: %@", scopes];

  self.signInButton.hidden = YES;
  self.signOutButton.hidden = NO;
}

- (IBAction)didTapSignOut {
  [GIDSignIn.sharedInstance signOut];

  self.mainLabel.text = @"Please sign in.";
  self.subLabel.text = @"";

  self.signInButton.hidden = NO;
  self.signOutButton.hidden = YES;
}

@end
