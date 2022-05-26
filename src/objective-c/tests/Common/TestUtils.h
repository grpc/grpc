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

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/**
 * Common utility to fetch plain text local interop server address.
 *
 * @return Interop test server address including host and port.
 */
FOUNDATION_EXPORT NSString *GRPCGetLocalInteropTestServerAddressPlainText(void);

/**
 * Common utility to fetch ssl local interop server address.
 *
 * @return Interop test server address including host and port.
 */
FOUNDATION_EXPORT NSString *GRPCGetLocalInteropTestServerAddressSSL(void);

/**
 * Common utility to fetch remote interop test server address.
 *
 * @return Interop test server address including host and port.
 */
FOUNDATION_EXPORT NSString *GRPCGetRemoteInteropTestServerAddress(void);

NS_ASSUME_NONNULL_END
