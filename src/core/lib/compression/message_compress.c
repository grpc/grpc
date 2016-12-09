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

#include "src/core/lib/compression/message_compress.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include <zlib.h>

#include "src/core/lib/slice/slice_internal.h"

#define OUTPUT_BLOCK_SIZE 1024

static int zlib_body(grpc_exec_ctx* exec_ctx, z_stream* zs,
                     grpc_slice_buffer* input, grpc_slice_buffer* output,
                     int (*flate)(z_stream* zs, int flush)) {
  int r;
  int flush;
  size_t i;
  grpc_slice outbuf = grpc_slice_malloc(OUTPUT_BLOCK_SIZE);
  const uInt uint_max = ~(uInt)0;

  GPR_ASSERT(GRPC_SLICE_LENGTH(outbuf) <= uint_max);
  zs->avail_out = (uInt)GRPC_SLICE_LENGTH(outbuf);
  zs->next_out = GRPC_SLICE_START_PTR(outbuf);
  flush = Z_NO_FLUSH;
  for (i = 0; i < input->count; i++) {
    if (i == input->count - 1) flush = Z_FINISH;
    GPR_ASSERT(GRPC_SLICE_LENGTH(input->slices[i]) <= uint_max);
    zs->avail_in = (uInt)GRPC_SLICE_LENGTH(input->slices[i]);
    zs->next_in = GRPC_SLICE_START_PTR(input->slices[i]);
    do {
      if (zs->avail_out == 0) {
        grpc_slice_buffer_add_indexed(output, outbuf);
        outbuf = grpc_slice_malloc(OUTPUT_BLOCK_SIZE);
        GPR_ASSERT(GRPC_SLICE_LENGTH(outbuf) <= uint_max);
        zs->avail_out = (uInt)GRPC_SLICE_LENGTH(outbuf);
        zs->next_out = GRPC_SLICE_START_PTR(outbuf);
      }
      r = flate(zs, flush);
      if (r < 0 && r != Z_BUF_ERROR /* not fatal */) {
        gpr_log(GPR_INFO, "zlib error (%d)", r);
        goto error;
      }
    } while (zs->avail_out == 0);
    if (zs->avail_in) {
      gpr_log(GPR_INFO, "zlib: not all input consumed");
      goto error;
    }
  }

  GPR_ASSERT(outbuf.refcount);
  outbuf.data.refcounted.length -= zs->avail_out;
  grpc_slice_buffer_add_indexed(output, outbuf);

  return 1;

error:
  grpc_slice_unref_internal(exec_ctx, outbuf);
  return 0;
}

static void* zalloc_gpr(void* opaque, unsigned int items, unsigned int size) {
  return gpr_malloc(items * size);
}

static void zfree_gpr(void* opaque, void* address) { gpr_free(address); }

static int zlib_compress(grpc_exec_ctx* exec_ctx, grpc_slice_buffer* input,
                         grpc_slice_buffer* output, int gzip) {
  z_stream zs;
  int r;
  size_t i;
  size_t count_before = output->count;
  size_t length_before = output->length;
  memset(&zs, 0, sizeof(zs));
  zs.zalloc = zalloc_gpr;
  zs.zfree = zfree_gpr;
  r = deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | (gzip ? 16 : 0),
                   8, Z_DEFAULT_STRATEGY);
  GPR_ASSERT(r == Z_OK);
  r = zlib_body(exec_ctx, &zs, input, output, deflate) &&
      output->length < input->length;
  if (!r) {
    for (i = count_before; i < output->count; i++) {
      grpc_slice_unref_internal(exec_ctx, output->slices[i]);
    }
    output->count = count_before;
    output->length = length_before;
  }
  deflateEnd(&zs);
  return r;
}

static int zlib_decompress(grpc_exec_ctx* exec_ctx, grpc_slice_buffer* input,
                           grpc_slice_buffer* output, int gzip) {
  z_stream zs;
  int r;
  size_t i;
  size_t count_before = output->count;
  size_t length_before = output->length;
  memset(&zs, 0, sizeof(zs));
  zs.zalloc = zalloc_gpr;
  zs.zfree = zfree_gpr;
  r = inflateInit2(&zs, 15 | (gzip ? 16 : 0));
  GPR_ASSERT(r == Z_OK);
  r = zlib_body(exec_ctx, &zs, input, output, inflate);
  if (!r) {
    for (i = count_before; i < output->count; i++) {
      grpc_slice_unref_internal(exec_ctx, output->slices[i]);
    }
    output->count = count_before;
    output->length = length_before;
  }
  inflateEnd(&zs);
  return r;
}

static int copy(grpc_slice_buffer* input, grpc_slice_buffer* output) {
  size_t i;
  for (i = 0; i < input->count; i++) {
    grpc_slice_buffer_add(output, grpc_slice_ref_internal(input->slices[i]));
  }
  return 1;
}

static int compress_inner(grpc_exec_ctx* exec_ctx,
                          grpc_compression_algorithm algorithm,
                          grpc_slice_buffer* input, grpc_slice_buffer* output) {
  switch (algorithm) {
    case GRPC_COMPRESS_NONE:
      /* the fallback path always needs to be send uncompressed: we simply
         rely on that here */
      return 0;
    case GRPC_COMPRESS_DEFLATE:
      return zlib_compress(exec_ctx, input, output, 0);
    case GRPC_COMPRESS_GZIP:
      return zlib_compress(exec_ctx, input, output, 1);
    case GRPC_COMPRESS_ALGORITHMS_COUNT:
      break;
  }
  gpr_log(GPR_ERROR, "invalid compression algorithm %d", algorithm);
  return 0;
}

int grpc_msg_compress(grpc_exec_ctx* exec_ctx,
                      grpc_compression_algorithm algorithm,
                      grpc_slice_buffer* input, grpc_slice_buffer* output) {
  if (!compress_inner(exec_ctx, algorithm, input, output)) {
    copy(input, output);
    return 0;
  }
  return 1;
}

int grpc_msg_decompress(grpc_exec_ctx* exec_ctx,
                        grpc_compression_algorithm algorithm,
                        grpc_slice_buffer* input, grpc_slice_buffer* output) {
  switch (algorithm) {
    case GRPC_COMPRESS_NONE:
      return copy(input, output);
    case GRPC_COMPRESS_DEFLATE:
      return zlib_decompress(exec_ctx, input, output, 0);
    case GRPC_COMPRESS_GZIP:
      return zlib_decompress(exec_ctx, input, output, 1);
    case GRPC_COMPRESS_ALGORITHMS_COUNT:
      break;
  }
  gpr_log(GPR_ERROR, "invalid compression algorithm %d", algorithm);
  return 0;
}
