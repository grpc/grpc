/*
 *
 * Copyright 2019 gRPC authors.
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

#import <XCTest/XCTest.h>

NSTimeInterval const kWaitTime = 30;

@interface GrpcIosTestUITests : XCTestCase
@end

@implementation GrpcIosTestUITests {
  XCUIApplication *testApp;
  XCUIApplication *settingsApp;
}

- (void)setUp {
  self.continueAfterFailure = NO;
  [[[XCUIApplication alloc] init] launch];
  testApp = [[XCUIApplication alloc] initWithBundleIdentifier:@"io.grpc.GrpcIosTest"];
  settingsApp = [[XCUIApplication alloc] initWithBundleIdentifier:@"com.apple.Preferences"];
  [settingsApp activate];
  // Go back to the first page of Settings.
  XCUIElement *backButton = settingsApp.navigationBars.buttons.firstMatch;
  while (backButton.exists) {
    [backButton tap];
  }
  XCTAssert([settingsApp.navigationBars[@"Settings"] waitForExistenceWithTimeout:kWaitTime]);
  // Turn off airplane mode
  [self setAirplaneMode:NO];
}

- (void)tearDown {
}

- (void)doUnaryCall {
  [testApp activate];
  [testApp.buttons[@"Unary call"] tap];
}

- (void)doStreamingCall {
  [testApp activate];
  [testApp.buttons[@"Start streaming call"] tap];
  [testApp.buttons[@"Send Message"] tap];
  [testApp.buttons[@"Stop streaming call"] tap];
}

- (void)expectCallSuccess {
  XCTAssert([testApp.staticTexts[@"Call done"] waitForExistenceWithTimeout:kWaitTime]);
}

- (void)expectCallFailed {
  XCTAssert([testApp.staticTexts[@"Call failed"] waitForExistenceWithTimeout:kWaitTime]);
}

- (void)setAirplaneMode:(BOOL)to {
  [settingsApp activate];
  XCUIElement *mySwitch = settingsApp.tables.element.cells.switches[@"Airplane Mode"];
  BOOL from = [(NSString *)mySwitch.value boolValue];
  if (from != to) {
    [mySwitch tap];
    // wait for gRPC to detect the change
    sleep(10);
  }
  XCTAssert([(NSString *)mySwitch.value boolValue] == to);
}

- (void)testBackgroundBeforeUnaryCall {
  // Open test app
  [testApp activate];

  // Send test app to background
  [XCUIDevice.sharedDevice pressButton:XCUIDeviceButtonHome];
  sleep(5);

  // Bring test app to foreground and make a unary call. Call should succeed
  [self doUnaryCall];
  [self expectCallSuccess];
}

- (void)testBackgroundBeforeStreamingCall {
  // Open test app
  [testApp activate];

  // Send test app to background
  [XCUIDevice.sharedDevice pressButton:XCUIDeviceButtonHome];
  sleep(5);

  // Bring test app to foreground and make a streaming call. Call should succeed.
  [self doStreamingCall];
  [self expectCallSuccess];
}

- (void)testUnaryCallAfterNetworkFlap {
  // Open test app and make a unary call. Channel to server should be open after this.
  [self doUnaryCall];
  [self expectCallSuccess];

  // Toggle airplane mode on and off
  [self setAirplaneMode:YES];
  [self setAirplaneMode:NO];

  // Bring test app to foreground and make a unary call. The call should succeed
  [self doUnaryCall];
  [self expectCallSuccess];
}

- (void)testStreamingCallAfterNetworkFlap {
  // Open test app and make a unary call. Channel to server should be open after this.
  [self doUnaryCall];
  [self expectCallSuccess];

  // Toggle airplane mode on and off
  [self setAirplaneMode:YES];
  [self setAirplaneMode:NO];

  [self doStreamingCall];
  [self expectCallSuccess];
}

- (void)testUnaryCallWhileNetworkDown {
  // Open test app and make a unary call. Channel to server should be open after this.
  [self doUnaryCall];
  [self expectCallSuccess];

  // Turn on airplane mode
  [self setAirplaneMode:YES];

  // Unary call should fail
  [self doUnaryCall];
  [self expectCallFailed];

  // Turn off airplane mode
  [self setAirplaneMode:NO];

  // Unary call should succeed
  [self doUnaryCall];
  [self expectCallSuccess];
}

- (void)testStreamingCallWhileNetworkDown {
  // Open test app and make a unary call. Channel to server should be open after this.
  [self doUnaryCall];
  [self expectCallSuccess];

  // Turn on airplane mode
  [self setAirplaneMode:YES];

  // Streaming call should fail
  [self doStreamingCall];
  [self expectCallFailed];

  // Turn off airplane mode
  [self setAirplaneMode:NO];

  // Unary call should succeed
  [self doStreamingCall];
  [self expectCallSuccess];
}
@end
