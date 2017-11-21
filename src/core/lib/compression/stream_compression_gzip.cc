/*
 *
 * Copyright 2017 gRPC authors.
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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/compression/stream_compression_gzip.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"

#define OUTPUT_BLOCK_SIZE (1024)

typedef struct grpc_stream_compression_context_gzip {
  grpc_stream_compression_context base;

  z_stream zs;
  int (*flate)(z_stream* zs, int flush);
} grpc_stream_compression_context_gzip;

static bool gzip_flate(grpc_stream_compression_context_gzip* ctx,
                       grpc_slice_buffer* in, grpc_slice_buffer* out,
                       size_t* output_size, size_t max_output_size, int flush,
                       bool* end_of_context) {
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

static bool grpc_stream_compress_gzip(grpc_stream_compression_context* ctx,
                                      grpc_slice_buffer* in,
                                      grpc_slice_buffer* out,
                                      size_t* output_size,
                                      size_t max_output_size,
                                      grpc_stream_compression_flush flush) {
  if (ctx == nullptr) {
    return false;
  }
  grpc_stream_compression_context_gzip* gzip_ctx =
      (grpc_stream_compression_context_gzip*)ctx;
  GPR_ASSERT(gzip_ctx->flate == deflate);
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
  return gzip_flate(gzip_ctx, in, out, output_size, max_output_size, gzip_flush,
                    nullptr);
}

static bool grpc_stream_decompress_gzip(grpc_stream_compression_context* ctx,
                                        grpc_slice_buffer* in,
                                        grpc_slice_buffer* out,
                                        size_t* output_size,
                                        size_t max_output_size,
                                        bool* end_of_context) {
  if (ctx == nullptr) {
    return false;
  }
  grpc_stream_compression_context_gzip* gzip_ctx =
      (grpc_stream_compression_context_gzip*)ctx;
  GPR_ASSERT(gzip_ctx->flate == inflate);
  return gzip_flate(gzip_ctx, in, out, output_size, max_output_size,
                    Z_SYNC_FLUSH, end_of_context);
}

static grpc_stream_compression_context*
grpc_stream_compression_context_create_gzip(
    grpc_stream_compression_method method) {
  GPR_ASSERT(method == GRPC_STREAM_COMPRESSION_GZIP_COMPRESS ||
             method == GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS);
  grpc_stream_compression_context_gzip* gzip_ctx =
      (grpc_stream_compression_context_gzip*)gpr_zalloc(
          sizeof(grpc_stream_compression_context_gzip));
  int r;
  if (gzip_ctx == nullptr) {
    return nullptr;
  }
  if (method == GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS) {
    r = inflateInit2(&gzip_ctx->zs, 0x1F);
    gzip_ctx->flate = inflate;
  } else {
    r = deflateInit2(&gzip_ctx->zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 0x1F, 8,
                     Z_DEFAULT_STRATEGY);
    gzip_ctx->flate = deflate;
  }
  if (r != Z_OK) {
    gpr_free(gzip_ctx);
    return nullptr;
  }

  gzip_ctx->base.vtable = &grpc_stream_compression_gzip_vtable;
  return (grpc_stream_compression_context*)gzip_ctx;
}

static void grpc_stream_compression_context_destroy_gzip(
    grpc_stream_compression_context* ctx) {
  if (ctx == nullptr) {
    return;
  }
  grpc_stream_compression_context_gzip* gzip_ctx =
      (grpc_stream_compression_context_gzip*)ctx;
  if (gzip_ctx->flate == inflate) {
    inflateEnd(&gzip_ctx->zs);
  } else {
    deflateEnd(&gzip_ctx->zs);
  }
  gpr_free(ctx);
}

const grpc_stream_compression_vtable grpc_stream_compression_gzip_vtable = {
    grpc_stream_compress_gzip, grpc_stream_decompress_gzip,
    grpc_stream_compression_context_create_gzip,
    grpc_stream_compression_context_destroy_gzip};
