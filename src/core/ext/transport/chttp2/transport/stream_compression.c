/*
 *
 * Copyright 2017, Google Inc.
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
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/stream_compression.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"

#define OUTPUT_BLOCK_SIZE (1024)

static bool gzip_flate(grpc_stream_compression_context *ctx,
                       grpc_slice_buffer *in, grpc_slice_buffer *out,
                       size_t *output_size, size_t max_output_size, int flush,
                       bool *end_of_context) {
  GPR_ASSERT(flush == 0 || flush == Z_SYNC_FLUSH || flush == Z_FINISH);
  /* Full flush is not allowed when inflating. */
  GPR_ASSERT(!(ctx->flate == inflate && (flush == Z_FINISH)));

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  int r;
  bool eoc = false;
  size_t original_max_output_size = max_output_size;
  while (max_output_size > 0 && (in->length > 0 || flush) && !eoc) {
    size_t slice_size = max_output_size < OUTPUT_BLOCK_SIZE ? max_output_size
                                                            : OUTPUT_BLOCK_SIZE;
    grpc_slice slice_out = GRPC_SLICE_MALLOC(slice_size);
    ctx->zs.avail_out = (uInt)slice_size;
    ctx->zs.next_out = GRPC_SLICE_START_PTR(slice_out);
    while (ctx->zs.avail_out > 0 && in->length > 0 && !eoc) {
      grpc_slice slice = grpc_slice_buffer_take_first(in);
      ctx->zs.avail_in = (uInt)GRPC_SLICE_LENGTH(slice);
      ctx->zs.next_in = GRPC_SLICE_START_PTR(slice);
      r = ctx->flate(&ctx->zs, Z_NO_FLUSH);
      if (r < 0 && r != Z_BUF_ERROR) {
        gpr_log(GPR_ERROR, "zlib error (%d)", r);
        grpc_slice_unref_internal(&exec_ctx, slice_out);
        grpc_exec_ctx_finish(&exec_ctx);
        return false;
      } else if (r == Z_STREAM_END && ctx->flate == inflate) {
        eoc = true;
      }
      if (ctx->zs.avail_in > 0) {
        grpc_slice_buffer_undo_take_first(
            in,
            grpc_slice_sub(slice, GRPC_SLICE_LENGTH(slice) - ctx->zs.avail_in,
                           GRPC_SLICE_LENGTH(slice)));
      }
      grpc_slice_unref_internal(&exec_ctx, slice);
    }
    if (flush != 0 && ctx->zs.avail_out > 0 && !eoc) {
      GPR_ASSERT(in->length == 0);
      r = ctx->flate(&ctx->zs, flush);
      if (flush == Z_SYNC_FLUSH) {
        switch (r) {
          case Z_OK:
            /* Maybe flush is not complete; just made some partial progress. */
            if (ctx->zs.avail_out > 0) {
              flush = 0;
            }
            break;
          case Z_BUF_ERROR:
          case Z_STREAM_END:
            flush = 0;
            break;
          default:
            gpr_log(GPR_ERROR, "zlib error (%d)", r);
            grpc_slice_unref_internal(&exec_ctx, slice_out);
            grpc_exec_ctx_finish(&exec_ctx);
            return false;
        }
      } else if (flush == Z_FINISH) {
        switch (r) {
          case Z_OK:
          case Z_BUF_ERROR:
            /* Wait for the next loop to assign additional output space. */
            GPR_ASSERT(ctx->zs.avail_out == 0);
            break;
          case Z_STREAM_END:
            flush = 0;
            break;
          default:
            gpr_log(GPR_ERROR, "zlib error (%d)", r);
            grpc_slice_unref_internal(&exec_ctx, slice_out);
            grpc_exec_ctx_finish(&exec_ctx);
            return false;
        }
      }
    }

    if (ctx->zs.avail_out == 0) {
      grpc_slice_buffer_add(out, slice_out);
    } else if (ctx->zs.avail_out < slice_size) {
      slice_out.data.refcounted.length -= ctx->zs.avail_out;
      grpc_slice_buffer_add(out, slice_out);
    } else {
      grpc_slice_unref_internal(&exec_ctx, slice_out);
    }
    max_output_size -= (slice_size - ctx->zs.avail_out);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  if (end_of_context) {
    *end_of_context = eoc;
  }
  if (output_size) {
    *output_size = original_max_output_size - max_output_size;
  }
  return true;
}

bool grpc_stream_compress(grpc_stream_compression_context *ctx,
                          grpc_slice_buffer *in, grpc_slice_buffer *out,
                          size_t *output_size, size_t max_output_size,
                          grpc_stream_compression_flush flush) {
  GPR_ASSERT(ctx->flate == deflate);
  int gzip_flush;
  switch (flush) {
    case GRPC_STREAM_COMPRESSION_FLUSH_NONE:
      gzip_flush = 0;
      break;
    case GRPC_STREAM_COMPRESSION_FLUSH_SYNC:
      gzip_flush = Z_SYNC_FLUSH;
      break;
    case GRPC_STREAM_COMPRESSION_FLUSH_FINISH:
      gzip_flush = Z_FINISH;
      break;
    default:
      gzip_flush = 0;
  }
  return gzip_flate(ctx, in, out, output_size, max_output_size, gzip_flush,
                    NULL);
}

bool grpc_stream_decompress(grpc_stream_compression_context *ctx,
                            grpc_slice_buffer *in, grpc_slice_buffer *out,
                            size_t *output_size, size_t max_output_size,
                            bool *end_of_context) {
  GPR_ASSERT(ctx->flate == inflate);
  return gzip_flate(ctx, in, out, output_size, max_output_size, Z_SYNC_FLUSH,
                    end_of_context);
}

grpc_stream_compression_context *grpc_stream_compression_context_create(
    grpc_stream_compression_method method) {
  grpc_stream_compression_context *ctx =
      gpr_zalloc(sizeof(grpc_stream_compression_context));
  int r;
  if (ctx == NULL) {
    return NULL;
  }
  if (method == GRPC_STREAM_COMPRESSION_DECOMPRESS) {
    r = inflateInit2(&ctx->zs, 0x1F);
    ctx->flate = inflate;
  } else {
    r = deflateInit2(&ctx->zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 0x1F, 8,
                     Z_DEFAULT_STRATEGY);
    ctx->flate = deflate;
  }
  if (r != Z_OK) {
    gpr_free(ctx);
    return NULL;
  }

  return ctx;
}

void grpc_stream_compression_context_destroy(
    grpc_stream_compression_context *ctx) {
  if (ctx->flate == inflate) {
    inflateEnd(&ctx->zs);
  } else {
    deflateEnd(&ctx->zs);
  }
  gpr_free(ctx);
}
