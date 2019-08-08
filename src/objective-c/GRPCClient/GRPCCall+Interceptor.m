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

#import "GRPCCall+Interceptor.h"
#import "GRPCInterceptor.h"

static id<GRPCInterceptorFactory> globalInterceptorFactory = nil;
static NSLock *globalInterceptorLock = nil;
static dispatch_once_t onceToken;

@implementation GRPCCall2 (Interceptor)

+ (void)registerGlobalInterceptor:(id<GRPCInterceptorFactory>)interceptorFactory {
  if (interceptorFactory == nil) {
    return;
  }
  dispatch_once(&onceToken, ^{
    globalInterceptorLock = [[NSLock alloc] init];
  });
  [globalInterceptorLock lock];
  if (globalInterceptorFactory != nil) {
    [globalInterceptorLock unlock];
    [NSException raise:NSInternalInconsistencyException
                format:
                    @"Global interceptor is already registered. Only one global interceptor can be "
                    @"registered in a process."];
    return;
  }

  globalInterceptorFactory = interceptorFactory;
  [globalInterceptorLock unlock];
}

+ (id<GRPCInterceptorFactory>)globalInterceptorFactory {
  dispatch_once(&onceToken, ^{
    globalInterceptorLock = [[NSLock alloc] init];
  });
  id<GRPCInterceptorFactory> factory;
  [globalInterceptorLock lock];
  factory = globalInterceptorFactory;
  [globalInterceptorLock unlock];

  return factory;
}

@end
