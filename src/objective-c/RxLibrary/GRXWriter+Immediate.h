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

#import "GRXWriter.h"

@interface GRXWriter (Immediate)

/**
 * Returns a writer that pulls values from the passed NSEnumerator instance and pushes them to
 * its writeable. The NSEnumerator is released when it finishes.
 */
+ (instancetype)writerWithEnumerator:(NSEnumerator *)enumerator;

/**
 * Returns a writer that pushes to its writeable the successive values returned by the passed
 * block. When the block first returns nil, it is released.
 */
+ (instancetype)writerWithValueSupplier:(id (^)())block;

/**
 * Returns a writer that iterates over the values of the passed container and pushes them to
 * its writeable. The container is released when the iteration is over.
 *
 * Note that the usual speed gain of NSFastEnumeration over NSEnumerator results from not having to
 * call one method per element. Because GRXWriteable instances accept values one by one, that speed
 * gain doesn't happen here.
 */
+ (instancetype)writerWithContainer:(id<NSFastEnumeration>)container;

/**
 * Returns a writer that sends the passed value to its writeable and then finishes (releasing the
 * value).
 */
+ (instancetype)writerWithValue:(id)value;

/**
 * Returns a writer that, as part of its start method, sends the passed error to the writeable
 * (then releasing the error).
 */
+ (instancetype)writerWithError:(NSError *)error;

/**
 * Returns a writer that, as part of its start method, finishes immediately without sending any
 * values to its writeable.
 */
+ (instancetype)emptyWriter;

@end
