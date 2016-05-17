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

/** States of a writer. */
typedef NS_ENUM(NSInteger, GRXWriterState) {

  /**
   * The writer has not yet been given a writeable to which it can push its values. To have a writer
   * transition to the Started state, send it a startWithWriteable: message.
   *
   * A writer's state cannot be manually set to this value.
   */
  GRXWriterStateNotStarted,

  /** The writer might push values to the writeable at any moment. */
  GRXWriterStateStarted,

  /**
   * The writer is temporarily paused, and won't send any more values to the writeable unless its
   * state is set back to Started. The writer might still transition to the Finished state at any
   * moment, and is allowed to send writesFinishedWithError: to its writeable.
   */
  GRXWriterStatePaused,

  /**
   * The writer has released its writeable and won't interact with it anymore.
   *
   * One seldomly wants to set a writer's state to this value, as its writeable isn't notified with
   * a writesFinishedWithError: message. Instead, sending finishWithError: to the writer will make
   * it notify the writeable and then transition to this state.
   */
  GRXWriterStateFinished
};

/**
 * An GRXWriter object can produce, on demand, a sequence of values. The sequence may be produced
 * asynchronously, and it may consist of any number of elements, including none or an infinite
 * number.
 *
 * GRXWriter is the active dual of NSEnumerator. The difference between them is thus whether the
 * object plays an active or passive role during usage: A user of NSEnumerator pulls values off it,
 * and passes the values to a writeable. A user of GRXWriter, though, just gives it a writeable, and
 * the GRXWriter instance pushes values to the writeable. This makes this protocol suitable to
 * represent a sequence of future values, as well as collections with internal iteration.
 *
 * An instance of GRXWriter can start producing values after a writeable is passed to it. It can
 * also be commanded to finish the sequence immediately (with an optional error). Finally, it can be
 * asked to pause, and resumed later. All GRXWriter objects support pausing and early termination.
 *
 * Thread-safety:
 *
 * State transitions take immediate effect if the object is used from a single thread. Subclasses
 * might offer stronger guarantees.
 *
 * Unless otherwise indicated by a conforming subclass, no messages should be sent concurrently to a
 * GRXWriter. I.e., conforming classes aren't required to be thread-safe.
 */
@interface GRXWriter : NSObject

/**
 * This property can be used to query the current state of the writer, which determines how it might
 * currently use its writeable. Some state transitions can be triggered by setting this property to
 * the corresponding value, and that's useful for advanced use cases like pausing an writer. For
 * more details, see the documentation of the enum further down.
 */
@property(nonatomic) GRXWriterState state;

/**
 * Transition to the Started state, and start sending messages to the writeable (a reference to it
 * is retained). Messages to the writeable may be sent before the method returns, or they may be
 * sent later in the future. See GRXWriteable.h for the different messages a writeable can receive.
 *
 * If this writer draws its values from an external source (e.g. from the filesystem or from a
 * server), calling this method will commonly trigger side effects (like network connections).
 *
 * This method might only be called on writers in the NotStarted state.
 */
- (void)startWithWriteable:(id<GRXWriteable>)writeable;

/**
 * Send writesFinishedWithError:errorOrNil to the writeable. Then release the reference to it and
 * transition to the Finished state.
 *
 * This method might only be called on writers in the Started or Paused state.
 */
- (void)finishWithError:(NSError *)errorOrNil;
@end
