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
 * Methods to let tune down the security of gRPC connections for specific hosts. These shouldn't be
 * used in releases, but are sometimes needed for testing.
 */
@interface GRPCCall (Tests)

/**
 * Establish all SSL connections to the provided host using the passed SSL target name and the root
 * certificates found in the file at |certsPath|.
 *
 * Must be called before any gRPC call to that host is made. It's illegal to pass the same host to
 * more than one invocation of the methods of this category.
 */
+ (void)useTestCertsPath:(NSString *)certsPath
                testName:(NSString *)testName
                 forHost:(NSString *)host;

/**
 * Establish all connections to the provided host using cleartext instead of SSL.
 *
 * Must be called before any gRPC call to that host is made. It's illegal to pass the same host to
 * more than one invocation of the methods of this category.
 */
+ (void)useInsecureConnectionsForHost:(NSString *)host;

/**
 * Resets all host configurations to their default values, and flushes all connections from the
 * cache.
 */
+ (void)resetHostSettings;
@end
