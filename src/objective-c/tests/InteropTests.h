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

#import <XCTest/XCTest.h>

/**
 * Implements tests as described here:
 * https://github.com/grpc/grpc/blob/master/doc/interop-test-descriptions.md
 *
 * This is an abstract class that needs to be subclassed. See |+host|.
 */
@interface InteropTests : XCTestCase
/**
 * Host to send the RPCs to. The base implementation returns nil, which would make all tests to
 * fail.
 * Override in a subclass to perform these tests against a specific address.
 */
+ (NSString *)host;

/**
 * Bytes of overhead of test proto responses due to encoding. This is used to excercise the behavior
 * when responses are just above or below the max response size. For some reason, the local and
 * remote servers enconde responses with different overhead (?), so this is defined per-subclass.
 */
- (int32_t)encodingOverhead;
@end
