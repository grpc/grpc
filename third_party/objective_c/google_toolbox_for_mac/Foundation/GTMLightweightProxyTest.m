//
//  GTMLightweightProxyTest.m
//
//  Copyright 2006-2008 Google Inc.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not
//  use this file except in compliance with the License.  You may obtain a copy
//  of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
//  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
//  License for the specific language governing permissions and limitations under
//  the License.
//

#import "GTMSenTestCase.h"
#import "GTMLightweightProxy.h"

@interface GTMLightweightProxy (GTMLightweightProxyTest)
- (id)init;
@end

@interface GTMLightweightProxyTest : GTMTestCase
- (BOOL)returnYes;
@end

// Declare a non-existent method that we can call without compiler warnings.
@interface GTMLightweightProxyTest (GTMLightweightProxyTestMadeUpMethodDeclaration)
- (void)someMadeUpMethod;
@end

@implementation GTMLightweightProxyTest

- (void)testInit {
  id proxy = [[[GTMLightweightProxy alloc]
               initWithRepresentedObject:self] autorelease];
  XCTAssertNotNil(proxy);

  proxy = [[[GTMLightweightProxy alloc] init] autorelease];
  XCTAssertNotNil(proxy);
}

- (void)testProxy {
  id proxy
    = [[[GTMLightweightProxy alloc] initWithRepresentedObject:self] autorelease];
  XCTAssertEqualObjects(self, [proxy representedObject],
                        @"Represented object setup failed");

  // Check that it identifies itself as a proxy.
  XCTAssertTrue([proxy isProxy], @"Should identify as a proxy");
  // Check that it passes class requests on
  XCTAssertTrue([proxy isMemberOfClass:[self class]],
                @"Should pass class requests through");

  // Check that it claims to respond to its selectors.
  XCTAssertTrue([proxy respondsToSelector:@selector(initWithRepresentedObject:)],
                @"Claims not to respond to initWithRepresentedObject:");
  XCTAssertTrue([proxy respondsToSelector:@selector(representedObject)],
                @"Claims not to respond to representedObject:");
  XCTAssertTrue([proxy respondsToSelector:@selector(setRepresentedObject:)],
                @"Claims not to respond to setRepresentedObject:");
  // Check that it responds to its represented object's selectors
  XCTAssertTrue([proxy respondsToSelector:@selector(returnYes)],
                @"Claims not to respond to returnYes");
  // ... but not to made up selectors.
#if !(__IPHONE_OS_VERSION_MIN_REQUIRED == __IPHONE_3_2 || __IPHONE_OS_VERSION_MIN_REQUIRED == __IPHONE_4_0)
  // Exceptions thrown by - (void)doesNotRecognizeSelector:(SEL)aSelector
  // does not get caught on iOS 3.2 and greater.
  // http://openradar.appspot.com/radar?id=420401
  XCTAssertThrows([proxy someMadeUpMethod],
                  @"Calling a bogus method should throw");
#endif

  // Check that callthrough works.
  XCTAssertTrue([proxy returnYes],
                @"Calling through to the represented object failed");

  // Check that nilling out the represented object works.
  [proxy setRepresentedObject:nil];
  XCTAssertTrue([proxy respondsToSelector:@selector(setRepresentedObject:)],
                @"Claims not to respond to setRepresentedObject: after nilling"
                @" out represented object");
  XCTAssertFalse([proxy respondsToSelector:@selector(returnYes)],
                  @"Claims to respond to returnYes after nilling out represented"
                  @" object");
  // Calling through once the represented object is nil should fail silently
  XCTAssertNoThrow([proxy returnYes],
                    @"Calling through without a represented object should fail"
                    @" silently");

  // ... even when they are made up.
#if !(__IPHONE_OS_VERSION_MIN_REQUIRED == __IPHONE_3_2 || __IPHONE_OS_VERSION_MIN_REQUIRED == __IPHONE_4_0)
  // Exceptions thrown by - (void)doesNotRecognizeSelector:(SEL)aSelector
  // does not get caught on iOS 3.2 and greater.
  // http://openradar.appspot.com/radar?id=420401

  XCTAssertNoThrow([proxy someMadeUpMethod],
                   @"Calling a bogus method on a nilled proxy should not throw");
#endif

}

// Simple method to test calling through the proxy.
- (BOOL)returnYes {
  return YES;
}

@end
