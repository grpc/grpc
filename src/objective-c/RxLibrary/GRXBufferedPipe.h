/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
