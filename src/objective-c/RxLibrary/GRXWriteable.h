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

/**
 * A GRXWriteable is an object to which a sequence of values can be sent. The
 * sequence finishes with an optional error.
 */
@protocol GRXWriteable <NSObject>

/** Push the next value of the sequence to the receiving object. */
- (void)writeValue:(id)value;

/**
 * Signal that the sequence is completed, or that an error ocurred. After this
 * message is sent to the instance, neither it nor writeValue: may be
 * called again.
 */
- (void)writesFinishedWithError:(NSError *)errorOrNil;
@end

typedef void (^GRXValueHandler)(id value);
typedef void (^GRXCompletionHandler)(NSError *errorOrNil);
typedef void (^GRXSingleHandler)(id value, NSError *errorOrNil);
typedef void (^GRXEventHandler)(BOOL done, id value, NSError *error);

/**
 * Utility to create objects that conform to the GRXWriteable protocol, from
 * blocks that handle each of the two methods of the protocol.
 */
@interface GRXWriteable : NSObject<GRXWriteable>

+ (instancetype)writeableWithSingleHandler:(GRXSingleHandler)handler;
+ (instancetype)writeableWithEventHandler:(GRXEventHandler)handler;

- (instancetype)initWithValueHandler:(GRXValueHandler)valueHandler
                   completionHandler:(GRXCompletionHandler)completionHandler
    NS_DESIGNATED_INITIALIZER;
@end
