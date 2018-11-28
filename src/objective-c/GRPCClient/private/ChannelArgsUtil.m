/*
 *
 * Copyright 2018 gRPC authors.
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

#import "ChannelArgsUtil.h"

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include <limits.h>

static void *copy_pointer_arg(void *p) {
  // Add ref count to the object when making copy
  id obj = (__bridge id)p;
  return (__bridge_retained void *)obj;
}

static void destroy_pointer_arg(void *p) {
  // Decrease ref count to the object when destroying
  CFRelease((CFTypeRef)p);
}

static int cmp_pointer_arg(void *p, void *q) { return p == q; }

static const grpc_arg_pointer_vtable objc_arg_vtable = {copy_pointer_arg, destroy_pointer_arg,
                                                        cmp_pointer_arg};

void GRPCFreeChannelArgs(grpc_channel_args *channel_args) {
  for (size_t i = 0; i < channel_args->num_args; ++i) {
    grpc_arg *arg = &channel_args->args[i];
    gpr_free(arg->key);
    if (arg->type == GRPC_ARG_STRING) {
      gpr_free(arg->value.string);
    }
  }
  gpr_free(channel_args->args);
  gpr_free(channel_args);
}

grpc_channel_args *GRPCBuildChannelArgs(NSDictionary *dictionary) {
  if (dictionary.count == 0) {
    return NULL;
  }

  NSArray *keys = [dictionary allKeys];
  NSUInteger argCount = [keys count];

  grpc_channel_args *channelArgs = gpr_malloc(sizeof(grpc_channel_args));
  channelArgs->args = gpr_malloc(argCount * sizeof(grpc_arg));

  // TODO(kriswuollett) Check that keys adhere to GRPC core library requirements

  NSUInteger j = 0;
  for (NSUInteger i = 0; i < argCount; ++i) {
    grpc_arg *arg = &channelArgs->args[j];
    arg->key = gpr_strdup([keys[i] UTF8String]);

    id value = dictionary[keys[i]];
    if ([value respondsToSelector:@selector(UTF8String)]) {
      arg->type = GRPC_ARG_STRING;
      arg->value.string = gpr_strdup([value UTF8String]);
      j++;
    } else if ([value respondsToSelector:@selector(intValue)]) {
      int64_t value64 = [value longLongValue];
      if (value64 <= INT_MAX || value64 >= INT_MIN) {
        arg->type = GRPC_ARG_INTEGER;
        arg->value.integer = (int)value64;
        j++;
      }
    } else if (value != nil) {
      arg->type = GRPC_ARG_POINTER;
      arg->value.pointer.p = (__bridge_retained void *)value;
      arg->value.pointer.vtable = &objc_arg_vtable;
      j++;
    }
  }
  channelArgs->num_args = j;

  return channelArgs;
}
