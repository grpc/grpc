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
#import "GRPCRequestHeaders.h"
#import "GRPCCall.h"
#import "NSDictionary+GRPC.h"

static NSString* normalizeKey(NSString* key) {
  if ([key canBeConvertedToEncoding:NSASCIIStringEncoding]) {
    return [key lowercaseString];
  } else {
    return nil;
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

@implementation GRPCRequestHeaders {
  __weak GRPCCall *_call;
  NSMutableDictionary *_proxy;
}

- (instancetype) initWithCall:(GRPCCall *)call {
  self = [super init];
  if (self) {
    _call = call;
    _proxy = [NSMutableDictionary dictionary];
  }
  return self;
}

- (id) objectForKeyedSubscript:(NSString *)key {
  NSString *normalizedKey = normalizeKey(key);
  if (normalizedKey) {
    return _proxy[normalizedKey];
  } else {
    return [NSNull null];
  }
}

- (void) setObject:(id)obj forKeyedSubscript:(NSString *)key {
  if (_call.state == GRXWriterStateNotStarted) {
    NSString *normalizedKey = normalizeKey(key);
    if (normalizedKey) {
      if (isKeyValuePairValid(key, obj)) {
        _proxy[normalizedKey] = obj;
      } else {
        [NSException raise:@"Invalid key/value pair"
                    format:@"Key %@ could not be added with value %@", key, obj];
      }
    } else {
      [NSException raise:@"Invalid key" format:@"Key %@ contains illegal characters", key];
    }
  } else {
    [NSException raise:@"Invalid modification"
                format:@"Cannot modify request metadata after call is started"];
  }
}

- (void) removeObjectForKey:(NSString *)aKey {
  if (_call.state == GRXWriterStateNotStarted) {
    NSString *normalizedKey = normalizeKey(aKey);
    if (normalizedKey) {
      [_proxy removeObjectForKey:normalizedKey];
    } else {
      [NSException raise:@"Invalid key" format:@"Key %@ contains illegal characters", aKey];
    }
  } else {
    [NSException raise:@"Invalid modification"
                format:@"Cannot modify request metadata after call is started"];
  }
}

- (void) removeAllObjects {
  if (_call.state == GRXWriterStateNotStarted) {
    [_proxy removeAllObjects];
  } else {
    [NSException raise:@"Invalid modification"
                format:@"Cannot modify request metadata after call is started"];
  }
}

// TODO(jcanizales): Just forward all invocations?

- (NSUInteger)count {
  return _proxy.count;
}

- (grpc_metadata *)grpc_metadataArray {
  return _proxy.grpc_metadataArray;
}
@end