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

#include <grpc/grpc.h>
#include "src/core/channel/channel_args.h"
#include "src/core/support/string.h"

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include <string.h>

static grpc_arg copy_arg(const grpc_arg *src) {
  grpc_arg dst;
  dst.type = src->type;
  dst.key = gpr_strdup(src->key);
  switch (dst.type) {
    case GRPC_ARG_STRING:
      dst.value.string = gpr_strdup(src->value.string);
      break;
    case GRPC_ARG_INTEGER:
      dst.value.integer = src->value.integer;
      break;
    case GRPC_ARG_POINTER:
      dst.value.pointer = src->value.pointer;
      dst.value.pointer.p = src->value.pointer.copy
                                ? src->value.pointer.copy(src->value.pointer.p)
                                : src->value.pointer.p;
      break;
  }
  return dst;
}

grpc_channel_args *grpc_channel_args_copy_and_add(const grpc_channel_args *src,
                                                  const grpc_arg *to_add) {
  grpc_channel_args *dst = gpr_malloc(sizeof(grpc_channel_args));
  size_t i;
  size_t src_num_args = (src == NULL) ? 0 : src->num_args;
  if (!src && !to_add) {
    dst->num_args = 0;
    dst->args = NULL;
    return dst;
  }
  dst->num_args = src_num_args + ((to_add == NULL) ? 0 : 1);
  dst->args = gpr_malloc(sizeof(grpc_arg) * dst->num_args);
  for (i = 0; i < src_num_args; i++) {
    dst->args[i] = copy_arg(&src->args[i]);
  }
  if (to_add != NULL) dst->args[src_num_args] = copy_arg(to_add);
  return dst;
}

grpc_channel_args *grpc_channel_args_copy(const grpc_channel_args *src) {
  return grpc_channel_args_copy_and_add(src, NULL);
}

void grpc_channel_args_destroy(grpc_channel_args *a) {
  size_t i;
  for (i = 0; i < a->num_args; i++) {
    switch (a->args[i].type) {
      case GRPC_ARG_STRING:
        gpr_free(a->args[i].value.string);
        break;
      case GRPC_ARG_INTEGER:
        break;
      case GRPC_ARG_POINTER:
        if (a->args[i].value.pointer.destroy) {
          a->args[i].value.pointer.destroy(a->args[i].value.pointer.p);
        }
        break;
    }
    gpr_free(a->args[i].key);
  }
  gpr_free(a->args);
  gpr_free(a);
}

int grpc_channel_args_is_census_enabled(const grpc_channel_args *a) {
  unsigned i;
  if (a == NULL) return 0;
  for (i = 0; i < a->num_args; i++) {
    if (0 == strcmp(a->args[i].key, GRPC_ARG_ENABLE_CENSUS)) {
      return a->args[i].value.integer != 0;
    }
  }
  return 0;
}

grpc_compression_level grpc_channel_args_get_compression_level(
    const grpc_channel_args *a) {
  size_t i;
  if (a) {
    for (i = 0; a && i < a->num_args; ++i) {
      if (a->args[i].type == GRPC_ARG_INTEGER &&
          !strcmp(GRPC_COMPRESSION_LEVEL_ARG, a->args[i].key)) {
        return a->args[i].value.integer;
        break;
      }
    }
  }
  return GRPC_COMPRESS_LEVEL_NONE;
}

grpc_channel_args *grpc_channel_args_set_compression_level(
    grpc_channel_args *a, grpc_compression_level level) {
  grpc_arg tmp;
  tmp.type = GRPC_ARG_INTEGER;
  tmp.key = GRPC_COMPRESSION_LEVEL_ARG;
  tmp.value.integer = level;
  return grpc_channel_args_copy_and_add(a, &tmp);
}
