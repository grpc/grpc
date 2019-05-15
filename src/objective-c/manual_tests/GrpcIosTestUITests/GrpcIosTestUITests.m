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
int const kNumIterations = 1;

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
  [testApp activate];
  // Reset RPC counter
  [self pressButton:@"Reset counter"];

  settingsApp = [[XCUIApplication alloc] initWithBundleIdentifier:@"com.apple.Preferences"];
  [settingsApp activate];
  [NSThread sleepForTimeInterval:1];
  // Go back to the first page of Settings.
  XCUIElement *backButton = settingsApp.navigationBars.buttons.firstMatch;
  while (backButton.exists && backButton.isHittable) {
    NSLog(@"Tapping back button");
    [backButton tap];
  }
  XCTAssert([settingsApp.navigationBars[@"Settings"] waitForExistenceWithTimeout:kWaitTime]);
  NSLog(@"Turning off airplane mode");
  // Turn off airplane mode
  [self setAirplaneMode:NO];

  // Turn on wifi
  NSLog(@"Turning on wifi");
  [self setWifi:YES];
}

- (void)tearDown {
}

- (void)doUnaryCall {
  [testApp activate];
  [self pressButton:@"Unary call"];
}

- (void)do10UnaryCalls {
  [testApp activate];
  [self pressButton:@"10 Unary calls"];
}

- (void)pressButton:(NSString *)name {
  // Wait for button to be visible
  while (![testApp.buttons[name] exists] || ![testApp.buttons[name] isHittable]) {
    [NSThread sleepForTimeInterval:1];
  }
  // Wait until all events in run loop have been processed
  while (CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true) == kCFRunLoopRunHandledSource)
    ;

  NSLog(@"Pressing button: %@", name);
  [testApp.buttons[name] tap];
}

- (void)expectCallSuccess {
  XCTAssert([testApp.staticTexts[@"Call done"] waitForExistenceWithTimeout:kWaitTime]);
}

- (void)expectCallFailed {
  XCTAssert([testApp.staticTexts[@"Call failed"] waitForExistenceWithTimeout:kWaitTime]);
}

- (void)expectCallSuccessOrFailed {
  NSDate *startTime = [NSDate date];
  while (![testApp.staticTexts[@"Call done"] exists] &&
         ![testApp.staticTexts[@"Call failed"] exists]) {
    XCTAssertLessThan([[NSDate date] timeIntervalSinceDate:startTime], kWaitTime);
    [NSThread sleepForTimeInterval:1];
  }
}

- (void)setAirplaneMode:(BOOL)to {
  [settingsApp activate];
  XCUIElement *mySwitch = settingsApp.tables.element.cells.switches[@"Airplane Mode"];
  BOOL from = [(NSString *)mySwitch.value boolValue];
  NSLog(@"Setting airplane from: %d to: %d", from, to);
  if (from != to) {
    [mySwitch tap];
    // wait for network change to finish
    [NSThread sleepForTimeInterval:5];
  }
  XCTAssert([(NSString *)mySwitch.value boolValue] == to);
}
- (void)setWifi:(BOOL)to {
  [settingsApp activate];
  [settingsApp.tables.element.cells.staticTexts[@"Wi-Fi"] tap];
  XCUIElement *wifiSwitch = settingsApp.tables.cells.switches[@"Wi-Fi"];
  BOOL from = [(NSString *)wifiSwitch.value boolValue];
  NSLog(@"Setting wifi from: %d to: %d", from, to);
  if (from != to) {
    [wifiSwitch tap];
    // wait for wifi networks to be detected
    [NSThread sleepForTimeInterval:10];
  }
  // Go back to the first page of Settings.
  XCUIElement *backButton = settingsApp.navigationBars.buttons.firstMatch;
  [backButton tap];
}

- (int)getRandomNumberBetween:(int)min max:(int)max {
  return min + arc4random_uniform((max - min + 1));
}

- (void)testBackgroundBeforeCall {
  NSLog(@"%s", __func__);
  // Open test app
  [testApp activate];
  // Send test app to background
  [XCUIDevice.sharedDevice pressButton:XCUIDeviceButtonHome];

  // Wait a bit
  int sleepTime = [self getRandomNumberBetween:5 max:10];
  NSLog(@"Sleeping for %d seconds", sleepTime);
  [NSThread sleepForTimeInterval:sleepTime];

  // Bring test app to foreground and make a unary call. Call should succeed
  [self doUnaryCall];
  [self expectCallSuccess];
}

- (void)testBackgroundDuringStreamingCall {
  NSLog(@"%s", __func__);
  // Open test app and start a streaming call
  [testApp activate];
  [self pressButton:@"Start streaming call"];

  // Send test app to background
  [XCUIDevice.sharedDevice pressButton:XCUIDeviceButtonHome];

  // Wait a bit
  int sleepTime = [self getRandomNumberBetween:5 max:10];
  NSLog(@"Sleeping for %d seconds", sleepTime);
  [NSThread sleepForTimeInterval:sleepTime];

  // Bring test app to foreground and make a streaming call. Call should succeed.
  [testApp activate];
  [self pressButton:@"Send Message"];
  [self pressButton:@"Stop streaming call"];
  [self expectCallSuccess];
}

- (void)testCallAfterNetworkFlap {
  NSLog(@"%s", __func__);
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

- (void)testCallWhileNetworkDown {
  NSLog(@"%s", __func__);
  // Open test app and make a unary call. Channel to server should be open after this.
  [self doUnaryCall];
  [self expectCallSuccess];

  // Turn on airplane mode
  [self setAirplaneMode:YES];
  // Turn off wifi
  [self setWifi:NO];

  // Unary call should fail
  [self doUnaryCall];
  [self expectCallFailed];

  // Turn off airplane mode
  [self setAirplaneMode:NO];
  // Turn on wifi
  [self setWifi:YES];

  // Unary call should succeed
  [self doUnaryCall];
  [self expectCallSuccess];
}

- (void)testSwitchApp {
  NSLog(@"%s", __func__);
  // Open test app and make a unary call. Channel to server should be open after this.
  [self doUnaryCall];
  [self expectCallSuccess];

  // Send test app to background
  [XCUIDevice.sharedDevice pressButton:XCUIDeviceButtonHome];

  // Open stocks app
  XCUIApplication *stocksApp =
      [[XCUIApplication alloc] initWithBundleIdentifier:@"com.apple.stocks"];
  [stocksApp activate];
  // Ensure that stocks app is running in the foreground
  XCTAssert([stocksApp waitForState:XCUIApplicationStateRunningForeground timeout:5]);
  // Wait a bit
  int sleepTime = [self getRandomNumberBetween:5 max:10];
  NSLog(@"Sleeping for %d seconds", sleepTime);
  [NSThread sleepForTimeInterval:sleepTime];
  [stocksApp terminate];

  // Make another unary call
  [self doUnaryCall];
  [self expectCallSuccess];
}

- (void)testNetworkFlapDuringStreamingCall {
  NSLog(@"%s", __func__);
  // Open test app and make a unary call. Channel to server should be open after this.
  [self doUnaryCall];
  [self expectCallSuccess];
  // Start streaming call and send a message
  [self pressButton:@"Start streaming call"];
  [self pressButton:@"Send Message"];

  // Toggle network on and off
  [self setAirplaneMode:YES];
  [self setWifi:NO];
  [self setAirplaneMode:NO];
  [self setWifi:YES];

  [testApp activate];
  [self pressButton:@"Stop streaming call"];
  // The call will fail if the stream gets a read error, else the call will succeed.
  [self expectCallSuccessOrFailed];

  // Make another unary call, it should succeed
  [self doUnaryCall];
  [self expectCallSuccess];
}

- (void)testConcurrentCalls {
  NSLog(@"%s", __func__);

  // Press button to start 10 unary calls
  [self do10UnaryCalls];

  // Toggle airplane mode on and off
  [self setAirplaneMode:YES];
  [self setAirplaneMode:NO];

  // 10 calls should have completed
  [testApp activate];
  XCTAssert([testApp.staticTexts[@"Calls completed: 10"] waitForExistenceWithTimeout:kWaitTime]);
}

- (void)invokeTest {
  for (int i = 0; i < kNumIterations; i++) {
    [super invokeTest];
  }
}

- (void)testUnaryCallTurnOffWifi {
  NSLog(@"%s", __func__);
  // Open test app and make a unary call. Channel to server should be open after this.
  [self doUnaryCall];
  [self expectCallSuccess];

  // Turn off wifi
  [self setWifi:NO];

  // Phone should switch to cellular connection, call should succeed
  [self doUnaryCall];
  [self expectCallSuccess];

  // Turn on wifi
  [self setWifi:YES];

  // Call should succeed after turning wifi back on
  [self doUnaryCall];
  [self expectCallSuccess];
}

- (void)testStreamingCallTurnOffWifi {
  NSLog(@"%s", __func__);
  // Open test app and make a unary call. Channel to server should be open after this.
  [self doUnaryCall];
  [self expectCallSuccess];

  // Start streaming call and send a message
  [self pressButton:@"Start streaming call"];
  [self pressButton:@"Send Message"];

  // Turn off wifi
  [self setWifi:NO];

  // Phone should switch to cellular connection, this results in the call failing
  [testApp activate];
  [self pressButton:@"Stop streaming call"];
  [self expectCallFailed];

  // Turn on wifi
  [self setWifi:YES];

  // Call should succeed after turning wifi back on
  [self doUnaryCall];
  [self expectCallSuccess];
}

@end
