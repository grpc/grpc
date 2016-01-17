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

#import "GRXWriter.h"
#import "GRXWriteable.h"

/**
 * This is a thread-safe wrapper over a GRXWriteable instance. It lets one enqueue calls to a
 * GRXWriteable instance for the main thread, guaranteeing that writesFinishedWithError: is the last
 * message sent to it (no matter what messages are sent to the wrapper, in what order, nor from
 * which thread). It also guarantees that, if cancelWithError: is called from the main thread (e.g.
 * by the app cancelling the writes), no further messages are sent to the writeable except
 * writesFinishedWithError:.
 *
 * TODO(jcanizales): Let the user specify another queue for the writeable callbacks.
 */
@interface GRXConcurrentWriteable : NSObject

/**
 * The GRXWriteable passed is the wrapped writeable.
 * The GRXWriteable instance is retained until writesFinishedWithError: is sent to it, and released
 * after that.
 */
- (instancetype)initWithWriteable:(id<GRXWriteable>)writeable NS_DESIGNATED_INITIALIZER;

/**
 * Enqueues writeValue: to be sent to the writeable in the main thread.
 * The passed handler is invoked from the main thread after writeValue: returns.
 */
- (void)enqueueValue:(id)value completionHandler:(void (^)())handler;

/**
 * Enqueues writesFinishedWithError:nil to be sent to the writeable in the main thread. After that
 * message is sent to the writeable, all other methods of this object are effectively noops.
 */
- (void)enqueueSuccessfulCompletion;

/**
 * If the writeable has not yet received a writesFinishedWithError: message, this will enqueue one
 * to be sent to it in the main thread, and cancel all other pending messages to the writeable
 * enqueued by this object (both past and future).
 * The error argument cannot be nil.
 */
- (void)cancelWithError:(NSError *)error;

/**
 * Cancels all pending messages to the writeable enqueued by this object (both past and future).
 * Because the writeable won't receive writesFinishedWithError:, this also releases the writeable.
 */
- (void)cancelSilently;
@end
