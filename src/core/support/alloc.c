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

#include <grpc/support/alloc.h>

#include <stdlib.h>
#include <grpc/support/port_platform.h>

void *gpr_malloc(size_t size) {
  void *p = malloc(size);
  if (!p) {
    abort();
  }
  return p;
}

void gpr_free(void *p) { free(p); }

void *gpr_realloc(void *p, size_t size) {
  p = realloc(p, size);
  if (!p) {
    abort();
  }
  return p;
}

void *gpr_malloc_aligned(size_t size, size_t alignment_log) {
  size_t alignment = ((size_t)1) << alignment_log;
  size_t extra = alignment - 1 + sizeof(void *);
  void *p = gpr_malloc(size + extra);
  void **ret = (void **)(((gpr_uintptr)p + extra) & ~(alignment - 1));
  ret[-1] = p;
  return (void *)ret;
}

void gpr_free_aligned(void *ptr) { free(((void **)ptr)[-1]); }
