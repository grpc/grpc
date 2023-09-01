/*
 *
 * Copyright 2019 gRPC authors.
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

#import "GRPCTransport.h"

static const GRPCTransportID gGRPCCoreSecureID = "io.grpc.transport.core.secure";
static const GRPCTransportID gGRPCCoreInsecureID = "io.grpc.transport.core.insecure";

const struct GRPCDefaultTransportImplList GRPCDefaultTransportImplList = {
    .core_secure = gGRPCCoreSecureID, .core_insecure = gGRPCCoreInsecureID};

static const GRPCTransportID gDefaultTransportID = gGRPCCoreSecureID;

static GRPCTransportRegistry *gTransportRegistry = nil;
static dispatch_once_t initTransportRegistry;

BOOL TransportIDIsEqual(GRPCTransportID lhs, GRPCTransportID rhs) {
  // Directly comparing pointers works because we require users to use the id provided by each
  // implementation, not coming up with their own string.
  return lhs == rhs;
}

NSUInteger TransportIDHash(GRPCTransportID transportID) {
  if (transportID == NULL) {
    transportID = gDefaultTransportID;
  }
  return [NSString stringWithCString:transportID encoding:NSUTF8StringEncoding].hash;
}

@implementation GRPCTransportRegistry {
  NSMutableDictionary<NSString *, id<GRPCTransportFactory>> *_registry;
  id<GRPCTransportFactory> _defaultFactory;
}

+ (instancetype)sharedInstance {
  dispatch_once(&initTransportRegistry, ^{
    gTransportRegistry = [[GRPCTransportRegistry alloc] init];
    NSAssert(gTransportRegistry != nil, @"Unable to initialize transport registry.");
    if (gTransportRegistry == nil) {
      NSLog(@"Unable to initialize transport registry.");
      [NSException raise:NSGenericException format:@"Unable to initialize transport registry."];
    }
  });
  return gTransportRegistry;
}

- (instancetype)init {
  if ((self = [super init])) {
    _registry = [NSMutableDictionary dictionary];
  }
  return self;
}

- (void)registerTransportWithID:(GRPCTransportID)transportID
                        factory:(id<GRPCTransportFactory>)factory {
  NSString *nsTransportID = [NSString stringWithCString:transportID encoding:NSUTF8StringEncoding];
  NSAssert(_registry[nsTransportID] == nil, @"The transport %@ has already been registered.",
           nsTransportID);
  if (_registry[nsTransportID] != nil) {
    NSLog(@"The transport %@ has already been registered.", nsTransportID);
    return;
  }
  _registry[nsTransportID] = factory;

  // if the default transport is registered, mark it.
  if (0 == strcmp(transportID, gDefaultTransportID)) {
    _defaultFactory = factory;
  }
}

- (id<GRPCTransportFactory>)getTransportFactoryWithID:(GRPCTransportID)transportID {
  if (transportID == NULL) {
    if (_defaultFactory == nil) {
      // fall back to default transport if no transport is provided
      [NSException raise:NSInvalidArgumentException
                  format:@"Did not specify transport and unable to find a default transport."];
      return nil;
    }
    return _defaultFactory;
  }
  NSString *nsTransportID = [NSString stringWithCString:transportID encoding:NSUTF8StringEncoding];
  id<GRPCTransportFactory> transportFactory = _registry[nsTransportID];
  if (transportFactory == nil) {
    if (_defaultFactory != nil) {
      // fall back to default transport if no transport is found
      NSLog(@"Unable to find transport with id %s; falling back to default transport.",
            transportID);
      return _defaultFactory;
    } else {
      [NSException raise:NSInvalidArgumentException
                  format:@"Unable to find transport with id %s", transportID];
      return nil;
    }
  }
  return transportFactory;
}

@end

@implementation GRPCTransport

- (dispatch_queue_t)dispatchQueue {
  [NSException raise:NSGenericException
              format:@"Implementations should override the dispatch queue"];
  return nil;
}

- (void)startWithRequestOptions:(nonnull GRPCRequestOptions *)requestOptions
                    callOptions:(nonnull GRPCCallOptions *)callOptions {
  [NSException raise:NSGenericException
              format:@"Implementations should override the methods of GRPCTransport"];
}

- (void)writeData:(nonnull id)data {
  [NSException raise:NSGenericException
              format:@"Implementations should override the methods of GRPCTransport"];
}

- (void)cancel {
  [NSException raise:NSGenericException
              format:@"Implementations should override the methods of GRPCTransport"];
}

- (void)finish {
  [NSException raise:NSGenericException
              format:@"Implementations should override the methods of GRPCTransport"];
}

- (void)receiveNextMessages:(NSUInteger)numberOfMessages {
  [NSException raise:NSGenericException
              format:@"Implementations should override the methods of GRPCTransport"];
}

@end
