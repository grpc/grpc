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

#import "GRPCRequestHeaders.h"

#import <Foundation/Foundation.h>

#import "NSDictionary+GRPC.h"

// Used by the setter.
static void CheckIsNonNilASCII(NSString *name, NSString* value) {
  if (!value) {
    [NSException raise:NSInvalidArgumentException format:@"%@ cannot be nil", name];
  }
  if (![value canBeConvertedToEncoding:NSASCIIStringEncoding]) {
    [NSException raise:NSInvalidArgumentException
                format:@"%@ %@ contains non-ASCII characters", name, value];
  }
}

// Precondition: key isn't nil.
static void CheckKeyValuePairIsValid(NSString *key, id value) {
  if ([key hasSuffix:@"-bin"]) {
    if (![value isKindOfClass:NSData.class]) {
      [NSException raise:NSInvalidArgumentException
                  format:@"Expected NSData value for header %@ ending in \"-bin\", "
       @"instead got %@", key, value];
    }
  } else {
    if (![value isKindOfClass:NSString.class]) {
      [NSException raise:NSInvalidArgumentException
                  format:@"Expected NSString value for header %@ not ending in \"-bin\", "
       @"instead got %@", key, value];
    }
    CheckIsNonNilASCII(@"Text header value", (NSString *)value);
  }
}

@implementation GRPCRequestHeaders {
  __weak GRPCCall *_call;
  // The NSMutableDictionary superclass doesn't hold any storage (so that people can implement their
  // own in subclasses). As that's not the reason we're subclassing, we just delegate storage to the
  // default NSMutableDictionary subclass returned by the cluster (e.g. __NSDictionaryM on iOS 9).
  NSMutableDictionary *_delegate;
}

- (instancetype)init {
  return [self initWithCall:nil];
}

- (instancetype)initWithCapacity:(NSUInteger)numItems {
  return [self init];
}

- (instancetype)initWithCoder:(NSCoder *)aDecoder {
  return [self init];
}

- (instancetype)initWithCall:(GRPCCall *)call {
  return [self initWithCall:call storage:[NSMutableDictionary dictionary]];
}

// Designated initializer
- (instancetype)initWithCall:(GRPCCall *)call storage:(NSMutableDictionary *)storage {
  // TODO(jcanizales): Throw if call or storage are nil.
  if ((self = [super init])) {
    _call = call;
    _delegate = storage;
  }
  return self;
}

- (instancetype)initWithObjects:(const id  _Nonnull __unsafe_unretained *)objects
                        forKeys:(const id<NSCopying>  _Nonnull __unsafe_unretained *)keys
                          count:(NSUInteger)cnt {
  return [self init];
}

- (void)checkCallIsNotStarted {
  if (_call.state != GRXWriterStateNotStarted) {
    [NSException raise:@"Invalid modification"
                format:@"Cannot modify request headers after call is started"];
  }
}

- (id)objectForKey:(NSString *)key {
  return _delegate[key.lowercaseString];
}

- (void)setObject:(id)obj forKey:(NSString *)key {
  [self checkCallIsNotStarted];
  CheckIsNonNilASCII(@"Header name", key);
  key = key.lowercaseString;
  CheckKeyValuePairIsValid(key, obj);
  _delegate[key] = obj;
}

- (void)removeObjectForKey:(NSString *)key {
  [self checkCallIsNotStarted];
  [_delegate removeObjectForKey:key.lowercaseString];
}

- (NSUInteger)count {
  return _delegate.count;
}

- (NSEnumerator * _Nonnull)keyEnumerator {
  return [_delegate keyEnumerator];
}

@end
