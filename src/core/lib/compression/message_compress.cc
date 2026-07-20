//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/lib/compression/message_compress.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <string.h>
#include <zconf.h>
#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <optional>

#include "src/core/lib/slice/slice.h"
#include "src/core/util/grpc_check.h"
#include "absl/log/log.h"

#define OUTPUT_BLOCK_SIZE 1024

enum class ZlibBodyResult { kOk, kError, kTooLarge };

static ZlibBodyResult zlib_body(z_stream* zs, grpc_slice_buffer* input,
                                grpc_slice_buffer* output,
                                int (*flate)(z_stream* zs, int flush),
                                std::optional<uint32_t> max_output_size) {
  int r = Z_STREAM_END;  // Do not fail on an empty input.
  int flush;
  size_t i;
  const size_t output_length_before = output->length;
  auto allocate_output_slice = [&]() {
    size_t block_size = OUTPUT_BLOCK_SIZE;
    if (max_output_size.has_value()) {
      const uint64_t max_plus_one = static_cast<uint64_t>(*max_output_size) + 1;
      const uint64_t output_length = output->length - output_length_before;
      GRPC_CHECK_LT(output_length, max_plus_one);
      block_size = static_cast<size_t>(
          std::min<uint64_t>(OUTPUT_BLOCK_SIZE, max_plus_one - output_length));
    }
    return GRPC_SLICE_MALLOC(block_size);
  };
  grpc_slice outbuf = allocate_output_slice();
  const uInt uint_max = ~uInt{0};

  GRPC_CHECK(GRPC_SLICE_LENGTH(outbuf) <= uint_max);
  zs->avail_out = static_cast<uInt> GRPC_SLICE_LENGTH(outbuf);
  zs->next_out = GRPC_SLICE_START_PTR(outbuf);
  flush = Z_NO_FLUSH;
  for (i = 0; i < input->count; i++) {
    if (i == input->count - 1) flush = Z_FINISH;
    GRPC_CHECK(GRPC_SLICE_LENGTH(input->slices[i]) <= uint_max);
    zs->avail_in = static_cast<uInt> GRPC_SLICE_LENGTH(input->slices[i]);
    zs->next_in = GRPC_SLICE_START_PTR(input->slices[i]);
    do {
      if (zs->avail_out == 0) {
        grpc_slice_buffer_add_indexed(output, outbuf);
        outbuf = allocate_output_slice();
        GRPC_CHECK(GRPC_SLICE_LENGTH(outbuf) <= uint_max);
        zs->avail_out = static_cast<uInt> GRPC_SLICE_LENGTH(outbuf);
        zs->next_out = GRPC_SLICE_START_PTR(outbuf);
      }
      r = flate(zs, flush);
      if (r < 0 && r != Z_BUF_ERROR /* not fatal */) {
        VLOG(2) << "zlib error (" << r << ")";
        goto error;
      }
      if (max_output_size.has_value()) {
        const size_t output_size = output->length - output_length_before +
                                   GRPC_SLICE_LENGTH(outbuf) - zs->avail_out;
        if (output_size > *max_output_size) goto too_large;
      }
    } while (zs->avail_out == 0);
    if (zs->avail_in) {
      VLOG(2) << "zlib: not all input consumed";
      goto error;
    }
  }
  if (r != Z_STREAM_END) {
    VLOG(2) << "zlib: Data error";
    goto error;
  }

  grpc_slice_buffer_add_indexed(
      output, grpc_slice_split_head(&outbuf,
                                    GRPC_SLICE_LENGTH(outbuf) - zs->avail_out));
  grpc_core::CSliceUnref(outbuf);

  return ZlibBodyResult::kOk;

error:
  grpc_core::CSliceUnref(outbuf);
  return ZlibBodyResult::kError;

too_large:
  grpc_core::CSliceUnref(outbuf);
  return ZlibBodyResult::kTooLarge;
}

static void* zalloc_gpr(void* /*opaque*/, unsigned int items,
                        unsigned int size) {
  return gpr_malloc(items * size);
}

static void zfree_gpr(void* /*opaque*/, void* address) { gpr_free(address); }

static int zlib_compress(grpc_slice_buffer* input, grpc_slice_buffer* output,
                         int gzip) {
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
  GRPC_CHECK(r == Z_OK);
  r = zlib_body(&zs, input, output, deflate, std::nullopt) ==
          ZlibBodyResult::kOk &&
      output->length < input->length;
  if (!r) {
    for (i = count_before; i < output->count; i++) {
      grpc_core::CSliceUnref(output->slices[i]);
    }
    output->count = count_before;
    output->length = length_before;
  }
  deflateEnd(&zs);
  return r;
}

static grpc_core::MessageDecompressionResult zlib_decompress(
    grpc_slice_buffer* input, grpc_slice_buffer* output, int gzip,
    std::optional<uint32_t> max_output_size) {
  z_stream zs;
  int r;
  ZlibBodyResult result;
  size_t i;
  size_t count_before = output->count;
  size_t length_before = output->length;
  memset(&zs, 0, sizeof(zs));
  zs.zalloc = zalloc_gpr;
  zs.zfree = zfree_gpr;
  r = inflateInit2(&zs, 15 | (gzip ? 16 : 0));
  GRPC_CHECK(r == Z_OK);
  result = zlib_body(&zs, input, output, inflate, max_output_size);
  if (result != ZlibBodyResult::kOk) {
    for (i = count_before; i < output->count; i++) {
      grpc_core::CSliceUnref(output->slices[i]);
    }
    output->count = count_before;
    output->length = length_before;
  }
  inflateEnd(&zs);
  if (result == ZlibBodyResult::kTooLarge) {
    return grpc_core::MessageDecompressionResult::kTooLarge;
  }
  return result == ZlibBodyResult::kOk
             ? grpc_core::MessageDecompressionResult::kOk
             : grpc_core::MessageDecompressionResult::kError;
}

static int copy(grpc_slice_buffer* input, grpc_slice_buffer* output) {
  size_t i;
  for (i = 0; i < input->count; i++) {
    grpc_slice_buffer_add(output, grpc_core::CSliceRef(input->slices[i]));
  }
  return 1;
}

static int compress_inner(grpc_compression_algorithm algorithm,
                          grpc_slice_buffer* input, grpc_slice_buffer* output) {
  switch (algorithm) {
    case GRPC_COMPRESS_NONE:
      // the fallback path always needs to be send uncompressed: we simply
      // rely on that here
      return 0;
    case GRPC_COMPRESS_DEFLATE:
      return zlib_compress(input, output, 0);
    case GRPC_COMPRESS_GZIP:
      return zlib_compress(input, output, 1);
    case GRPC_COMPRESS_ALGORITHMS_COUNT:
      break;
  }
  LOG(ERROR) << "invalid compression algorithm " << algorithm;
  return 0;
}

int grpc_msg_compress(grpc_compression_algorithm algorithm,
                      grpc_slice_buffer* input, grpc_slice_buffer* output) {
  if (!compress_inner(algorithm, input, output)) {
    copy(input, output);
    return 0;
  }
  return 1;
}

grpc_core::MessageDecompressionResult grpc_core::DecompressMessageWithLimit(
    grpc_compression_algorithm algorithm, grpc_slice_buffer* input,
    grpc_slice_buffer* output, std::optional<uint32_t> max_output_size) {
  switch (algorithm) {
    case GRPC_COMPRESS_NONE:
      if (max_output_size.has_value() && input->length > *max_output_size) {
        return MessageDecompressionResult::kTooLarge;
      }
      copy(input, output);
      return MessageDecompressionResult::kOk;
    case GRPC_COMPRESS_DEFLATE:
      return zlib_decompress(input, output, 0, max_output_size);
    case GRPC_COMPRESS_GZIP:
      return zlib_decompress(input, output, 1, max_output_size);
    case GRPC_COMPRESS_ALGORITHMS_COUNT:
      break;
  }
  LOG(ERROR) << "invalid compression algorithm " << algorithm;
  return MessageDecompressionResult::kError;
}

int grpc_msg_decompress(grpc_compression_algorithm algorithm,
                        grpc_slice_buffer* input, grpc_slice_buffer* output) {
  return grpc_core::DecompressMessageWithLimit(algorithm, input, output,
                                               std::nullopt) ==
         grpc_core::MessageDecompressionResult::kOk;
}
