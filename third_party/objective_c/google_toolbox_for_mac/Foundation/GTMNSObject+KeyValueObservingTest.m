//
//  GTMNSObject+KeyValueObservingTest.m
//
//  Copyright 2009 Google Inc.
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

//
//  Tester.m
//  MAKVONotificationCenter
//
//  Created by Michael Ash on 10/15/08.
//

// This code is based on code by Michael Ash.
// See comment in header.

#import "GTMSenTestCase.h"
#import "GTMNSObject+KeyValueObserving.h"
#import "GTMDefines.h"

@interface GTMNSObject_KeyValueObservingTest : GTMTestCase  {
  int32_t count_;
  NSMutableDictionary *dict_;
  GTM_WEAK NSString *expectedValue_;
}

- (void)observeValueChange:(GTMKeyValueChangeNotification *)notification;

@end

@implementation GTMNSObject_KeyValueObservingTest
- (void)setUp {
  dict_ = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
          @"foo", @"key",
          nil];
}

- (void)tearDown {
  [dict_ release];
}

- (void)testSingleChange {
  count_ = 0;
  [dict_ gtm_addObserver:self
             forKeyPath:@"key"
               selector:@selector(observeValueChange:)
               userInfo:@"userInfo"
                options:NSKeyValueObservingOptionNew];
  expectedValue_ = @"bar";
  [dict_ setObject:expectedValue_ forKey:@"key"];
  XCTAssertEqual(count_, (int32_t)1);
  [dict_ gtm_removeObserver:self
                 forKeyPath:@"key"
                  selector:@selector(observeValueChange:)];
  [dict_ setObject:@"foo" forKey:@"key"];
  XCTAssertEqual(count_, (int32_t)1);
}

- (void)testStopObservingAllKeyPaths {
  count_ = 0;
  [dict_ gtm_addObserver:self
              forKeyPath:@"key"
                selector:@selector(observeValueChange:)
                userInfo:@"userInfo"
                 options:NSKeyValueObservingOptionNew];
  expectedValue_ = @"bar";
  [dict_ setObject:expectedValue_ forKey:@"key"];
  XCTAssertEqual(count_, (int32_t)1);
  [self gtm_stopObservingAllKeyPaths];
  [dict_ setObject:@"foo" forKey:@"key"];
  XCTAssertEqual(count_, (int32_t)1);
}


- (void)testRemoving {
  [dict_ gtm_removeObserver:self
                 forKeyPath:@"key"
                   selector:@selector(observeValueChange:)];
}

- (void)testAdding {
  [dict_ gtm_addObserver:self
              forKeyPath:@"key"
                selector:@selector(observeValueChange:)
                userInfo:@"userInfo"
                 options:NSKeyValueObservingOptionNew];
  [dict_ gtm_addObserver:self
              forKeyPath:@"key"
                selector:@selector(observeValueChange:)
                userInfo:@"userInfo"
                 options:NSKeyValueObservingOptionNew];
  [dict_ gtm_removeObserver:self
                 forKeyPath:@"key"
                   selector:@selector(observeValueChange:)];
}

- (void)observeValueChange:(GTMKeyValueChangeNotification *)notification {
  XCTAssertEqualObjects([notification userInfo], @"userInfo");
  XCTAssertEqualObjects([notification keyPath], @"key");
  XCTAssertEqualObjects([notification object], dict_);
  NSDictionary *change = [notification change];
  NSString *value = [change objectForKey:NSKeyValueChangeNewKey];
  XCTAssertEqualObjects(value, expectedValue_);
  ++count_;

  GTMKeyValueChangeNotification *copy = [[notification copy] autorelease];
  XCTAssertEqualObjects(notification, copy);
  XCTAssertEqual([notification hash], [copy hash]);
}

@end
