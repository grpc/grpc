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
