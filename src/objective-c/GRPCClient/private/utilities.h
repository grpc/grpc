/*
 *
 * Copyright 2018 gRPC authors.
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

/** Raise exception when condition not met. Disregard NS_BLOCK_ASSERTIONS. */
#define GRPCAssert(condition, errorType, errorString) \
do { \
if (!(condition)) { \
[NSException raise:(errorType) format:(errorString)]; \
} \
} while (0)

/** The same as GRPCAssert but allows arguments to be put in the raised string. */
#define GRPCAssertWithArgument(condition, errorType, errorFormat, ...) \
    do { \
      if (!(condition)) { \
        [NSException raise:(errorType) format:(errorFormat), __VA_ARGS__]; \
      } \
    } while (0)
