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

#import <Foundation/Foundation.h>

#import "GRXWriteable.h"
#import "GRXWriter.h"

/**
 * A buffered pipe is a Writer that also acts as a Writeable.
 * Once it is started, whatever values are written into it (via -writeValue:) will be propagated
 * immediately, unless flow control prevents it.
 * If it is throttled and keeps receiving values, as well as if it receives values before being
 * started, it will buffer them and propagate them in order as soon as its state becomes Started.
 * If it receives an error (via -writesFinishedWithError:), it will drop any buffered values and
 * propagate the error immediately.
 *
 * Beware that a pipe of this type can't prevent receiving more values when it is paused (for
 * example if used to write data to a congested network connection). Because in such situations the
 * pipe will keep buffering all data written to it, your application could run out of memory and
 * crash. If you want to react to flow control signals to prevent that, instead of using this class
 * you can implement an object that conforms to GRXWriter.
 *
 * Thread-safety:
 * The methods of an object of this class should not be called concurrently from different threads.
 */
@interface GRXBufferedPipe : GRXWriter<GRXWriteable>

/** Convenience constructor. */
+ (instancetype)pipe;

@end
