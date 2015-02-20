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

@protocol GRXWriteable;
@protocol GRXWriter;

// This is a thread-safe wrapper over a GRXWriteable instance. It lets one
// enqueue calls to a GRXWriteable instance for the main thread, guaranteeing
// that didFinishWithError: is the last message sent to it (no matter what
// messages are sent to the wrapper, in what order, nor from which thread). It
// also guarantees that, if cancelWithError: is called from the main thread
// (e.g. by the app cancelling the writes), no further messages are sent to the
// writeable except didFinishWithError:.
//
// TODO(jcanizales): Let the user specify another queue for the writeable
// callbacks.
// TODO(jcanizales): Rename to GRXWriteableWrapper and move to the Rx library.
@interface GRPCDelegateWrapper : NSObject

// The GRXWriteable passed is the wrapped writeable.
// Both the GRXWriter instance and the GRXWriteable instance are retained until
// didFinishWithError: is sent to the writeable, and released after that.
// This is used to create a retain cycle that keeps both objects alive until the
// writing is explicitly finished.
- (instancetype)initWithWriteable:(id<GRXWriteable>)writeable writer:(id<GRXWriter>)writer
    NS_DESIGNATED_INITIALIZER;

// Enqueues didReceiveValue: to be sent to the writeable in the main thread.
// The passed handler is invoked from the main thread after didReceiveValue:
// returns.
- (void)enqueueMessage:(NSData *)message completionHandler:(void (^)())handler;

// Enqueues didFinishWithError:nil to be sent to the writeable in the main
// thread. After that message is sent to the writeable, all other methods of
// this object are effectively noops.
- (void)enqueueSuccessfulCompletion;

// If the writeable has not yet received a didFinishWithError: message, this
// will enqueue one to be sent to it in the main thread, and cancel all other
// pending messages to the writeable enqueued by this object (both past and
// future).
// The error argument cannot be nil.
- (void)cancelWithError:(NSError *)error;

// Cancels all pending messages to the writeable enqueued by this object (both
// past and future). Because the writeable won't receive didFinishWithError:,
// this also releases the writeable and the writer.
- (void)cancelSilently;
@end
