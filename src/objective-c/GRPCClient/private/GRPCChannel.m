/*
 *
 * Copyright 2015 gRPC authors.
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

#import "GRPCChannel.h"

#include <grpc/grpc_security.h>
#ifdef GRPC_COMPILE_WITH_CRONET
#include <grpc/grpc_cronet.h>
#endif
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#ifdef GRPC_COMPILE_WITH_CRONET
#import <Cronet/Cronet.h>
#import <GRPCClient/GRPCCall+Cronet.h>
#endif
#import "GRPCCompletionQueue.h"

static void* copy_pointer_arg(void *p) {
  // Add ref count to the object when making copy
  id obj = (__bridge id)p;
  return (__bridge_retained void *)obj;
}

static void destroy_pointer_arg(void *p) {
  // Decrease ref count to the object when destroying
  CFRelease((CFTreeRef)p);
}

static int cmp_pointer_arg(void *p, void *q) {
  return p == q;
}

static const grpc_arg_pointer_vtable objc_arg_vtable = {
  copy_pointer_arg, destroy_pointer_arg, cmp_pointer_arg};

static void FreeChannelArgs(grpc_channel_args *channel_args) {
  for (size_t i = 0; i < channel_args->num_args; ++i) {
    grpc_arg *arg = &channel_args->args[i];
    gpr_free(arg->key);
    if (arg->type == GRPC_ARG_STRING) {
      gpr_free(arg->value.string);
    }
  }
  gpr_free(channel_args);
}

/**
 * Allocates a @c grpc_channel_args and populates it with the options specified in the
 * @c dictionary. Keys must be @c NSString. If the value responds to @c @selector(UTF8String) then
 * it will be mapped to @c GRPC_ARG_STRING. If not, it will be mapped to @c GRPC_ARG_INTEGER if the
 * value responds to @c @selector(intValue). Otherwise, an exception will be raised. The caller of
 * this function is responsible for calling @c freeChannelArgs on a non-NULL returned value.
 */
static grpc_channel_args *BuildChannelArgs(NSDictionary *dictionary) {
  if (!dictionary) {
    return NULL;
  }

  NSArray *keys = [dictionary allKeys];
  NSUInteger argCount = [keys count];

  grpc_channel_args *channelArgs = gpr_malloc(sizeof(grpc_channel_args));
  channelArgs->num_args = argCount;
  channelArgs->args = gpr_malloc(argCount * sizeof(grpc_arg));

  // TODO(kriswuollett) Check that keys adhere to GRPC core library requirements

  for (NSUInteger i = 0; i < argCount; ++i) {
    grpc_arg *arg = &channelArgs->args[i];
    arg->key = gpr_strdup([keys[i] UTF8String]);

    id value = dictionary[keys[i]];
    if ([value respondsToSelector:@selector(UTF8String)]) {
      arg->type = GRPC_ARG_STRING;
      arg->value.string = gpr_strdup([value UTF8String]);
    } else if ([value respondsToSelector:@selector(intValue)]) {
      arg->type = GRPC_ARG_INTEGER;
      arg->value.integer = [value intValue];
    } else if (value != nil) {
      arg->type = GRPC_ARG_POINTER;
      arg->value.pointer.p = (__bridge_retained void *)value;
      arg->value.pointer.vtable = &objc_arg_vtable;
    } else {
      [NSException raise:NSInvalidArgumentException
                  format:@"Invalid value type: %@", [value class]];
    }
  }

  return channelArgs;
}

@implementation GRPCChannel {
  // Retain arguments to channel_create because they may not be used on the thread that invoked
  // the channel_create function.
  NSString *_host;
  grpc_channel_args *_channelArgs;
}

#ifdef GRPC_COMPILE_WITH_CRONET
- (instancetype)initWithHost:(NSString *)host
                cronetEngine:(stream_engine *)cronetEngine
                 channelArgs:(NSDictionary *)channelArgs {
  if (!host) {
    [NSException raise:NSInvalidArgumentException format:@"host argument missing"];
  }

  if (self = [super init]) {
    _channelArgs = BuildChannelArgs(channelArgs);
    _host = [host copy];
    _unmanagedChannel = grpc_cronet_secure_channel_create(cronetEngine,
                                                          _host.UTF8String,
                                                          _channelArgs,
                                                          NULL);
  }

  return self;
}
#endif

- (instancetype)initWithHost:(NSString *)host
                      secure:(BOOL)secure
                 credentials:(struct grpc_channel_credentials *)credentials
                 channelArgs:(NSDictionary *)channelArgs {
  if (!host) {
    [NSException raise:NSInvalidArgumentException format:@"host argument missing"];
  }

  if (secure && !credentials) {
    return nil;
  }

  if (self = [super init]) {
    _channelArgs = BuildChannelArgs(channelArgs);
    _host = [host copy];
    if (secure) {
      _unmanagedChannel = grpc_secure_channel_create(credentials, _host.UTF8String, _channelArgs,
                                                     NULL);
    } else {
      _unmanagedChannel = grpc_insecure_channel_create(_host.UTF8String, _channelArgs, NULL);
    }
  }

  return self;
}

- (void)dealloc {
  // TODO(jcanizales): Be sure to add a test with a server that closes the connection prematurely,
  // as in the past that made this call to crash.
  grpc_channel_destroy(_unmanagedChannel);
  FreeChannelArgs(_channelArgs);
}

#ifdef GRPC_COMPILE_WITH_CRONET
+ (GRPCChannel *)secureCronetChannelWithHost:(NSString *)host
                                 channelArgs:(NSDictionary *)channelArgs {
  stream_engine *engine = [GRPCCall cronetEngine];
  if (!engine) {
    [NSException raise:NSInvalidArgumentException
                format:@"cronet_engine is NULL. Set it first."];
    return nil;
  }
  return [[GRPCChannel alloc] initWithHost:host cronetEngine:engine channelArgs:channelArgs];
}
#endif

+ (GRPCChannel *)secureChannelWithHost:(NSString *)host {
  return [[GRPCChannel alloc] initWithHost:host secure:YES credentials:NULL channelArgs:NULL];
}

+ (GRPCChannel *)secureChannelWithHost:(NSString *)host
                           credentials:(struct grpc_channel_credentials *)credentials
                           channelArgs:(NSDictionary *)channelArgs {
  return [[GRPCChannel alloc] initWithHost:host
                                    secure:YES
                               credentials:credentials
                               channelArgs:channelArgs];

}

+ (GRPCChannel *)insecureChannelWithHost:(NSString *)host
                             channelArgs:(NSDictionary *)channelArgs {
  return [[GRPCChannel alloc] initWithHost:host
                                    secure:NO
                               credentials:NULL
                               channelArgs:channelArgs];
}

- (grpc_call *)unmanagedCallWithPath:(NSString *)path
                          serverName:(NSString *)serverName
                             timeout:(NSTimeInterval)timeout
                     completionQueue:(GRPCCompletionQueue *)queue {
  GPR_ASSERT(timeout >= 0);
  if (timeout < 0) {
    timeout = 0;
  }
  grpc_slice host_slice = grpc_empty_slice();
  if (serverName) {
    host_slice = grpc_slice_from_copied_string(serverName.UTF8String);
  }
  grpc_slice path_slice = grpc_slice_from_copied_string(path.UTF8String);
  gpr_timespec deadline_ms = timeout == 0 ?
                                gpr_inf_future(GPR_CLOCK_REALTIME) :
                                gpr_time_add(
                                    gpr_now(GPR_CLOCK_MONOTONIC),
                                    gpr_time_from_millis((int64_t)(timeout * 1000), GPR_TIMESPAN));
  grpc_call *call = grpc_channel_create_call(_unmanagedChannel,
                                             NULL, GRPC_PROPAGATE_DEFAULTS,
                                             queue.unmanagedQueue,
                                             path_slice,
                                             serverName ? &host_slice : NULL,
                                             deadline_ms, NULL);
  if (serverName) {
    grpc_slice_unref(host_slice);
  }
  grpc_slice_unref(path_slice);
  return call;
}

@end
