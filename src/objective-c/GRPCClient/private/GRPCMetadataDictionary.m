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

#import "GRPCHeaderDictionary.h"
#import "GRPCCall.h"

static NSString* normalizeKey(NSString* key) {
  NSString *asciiKey = [key dataUsingEncoding:NSASCIIStringEncoding];
  if (asciiKey == nil) {
    return nil;
  } else {
    return [asciiKey lowercaseString];
  }
}

static bool isKeyValuePairValid(NSString *key, id value) {
  if ([key hasSuffix:@"-bin"]) {
    if (![value isKindOfClass:[NSData class]]) {
      return false;
    }
  } else {
    if (![value isKindOfClass:[NSString class]]) {
      return false;
    }
  }
  return true;
}

@implementation GRPCHeaderDictionary {
  NSMutableDictionary *_proxy;
  __weak GRPCCall *_call;
}

- (instancetype)initWithObjects:(const id [])objects
                        forKeys:(const id<NSCopying> [])keys
                          count:(NSUInteger)count {
  NSMutableDictionary proxy = [NSMutableDictionary dictionary];
  for (int i = 0; i<count; i++) {
    if (![keys[i] isKindOfClass [NSString class]]) {
      [NSException raise:@"Invalid metadata key" format:@"Expected string keys, got %@", keys[i]];
    }
    NSString *normalizedKey = normalizeKey((NSString*)keys[i]);
    if (normalizedKey == nil) {
      [NSException raise:@"Invalid metadata key"
                  format:@"Metadata key %@ contained illegal characters", keys[i]];
    }
    if (isKeyValuePairValid(normalizedKey, objects[i])) {
      proxy[normalizedKey] = objects[i];
    } else {
      [NSException raise:@"Invalid metadata key/value pair"
                  format:@"Metadata key %@ cannot have value %@", keys[i], objects[i]];
    }
  }
  self = [super init];
  if (self) {
    _proxy = proxy;
  }
  return self;
}

- (instancetype)initWithCall:(GRPCCall*)call {
  self = [super init];
  if (self) {
    _proxy = [NSMutableDictionary dictionary];
    _call = call;
  }
  return self;
}

- (int)getCount() {
  return _proxy.count;
}



@end