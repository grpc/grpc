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
#include "src/core/util/status_helper.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#define OUTPUT_BLOCK_SIZE 1024

namespace grpc_core {
namespace {

absl::StatusOr<SliceBuffer> ZlibBody(z_stream* zs, const SliceBuffer& input,
                                     int (*flate)(z_stream* zs, int flush),
                                     std::optional<uint32_t> max_output_size) {
  int r = Z_STREAM_END;  // Do not fail on an empty input.
  int flush;
  size_t i;
  uint32_t remaining_output_size = max_output_size.value_or(UINT32_MAX);
  SliceBuffer output_sb;
  const uInt uint_max = ~uInt{0};
  auto outbuf = MutableSlice::CreateUninitialized(
      std::min<uint32_t>(remaining_output_size, OUTPUT_BLOCK_SIZE));

  GRPC_CHECK(outbuf.length() <= uint_max);
  zs->avail_out = static_cast<uInt>(outbuf.length());
  zs->next_out = const_cast<uint8_t*>(outbuf.begin());
  flush = Z_NO_FLUSH;
  for (i = 0; i < input.Count(); i++) {
    if (i == input.Count() - 1) {
      flush = Z_FINISH;
    }
    GRPC_CHECK(input[i].length() <= uint_max);
    zs->avail_in = static_cast<uInt>(input[i].length());
    zs->next_in = const_cast<uint8_t*>(input[i].begin());
    do {
      if (zs->avail_out == 0) {
        if (max_output_size.has_value() && remaining_output_size == 0) {
          VLOG(2) << "zlib: max provided output size exceeded";
          return absl::ResourceExhaustedError(
              "Decompressed message larger than max");
        }
        output_sb.AppendIndexed(Slice(std::move(outbuf)));
        outbuf = MutableSlice::CreateUninitialized(
            std::min<uint32_t>(remaining_output_size, OUTPUT_BLOCK_SIZE));
        // Update remaining output size to reflect the size of the slice we just
        // filled with compressed / decompressed data.
        if (max_output_size.has_value()) {
          remaining_output_size -= outbuf.length();
        }
        GRPC_CHECK(outbuf.length() <= uint_max);
        zs->avail_out = static_cast<uInt>(outbuf.length());
        zs->next_out = const_cast<uint8_t*>(outbuf.begin());
      }
      r = flate(zs, flush);
      if (r < 0 && r != Z_BUF_ERROR /* not fatal */) {
        VLOG(2) << "zlib error (" << r << ")";
        return absl::InternalError("Decompression failed due to zlib error");
      }
    } while (zs->avail_out == 0);
    if (zs->avail_in) {
      VLOG(2) << "zlib: not all input consumed";
      return absl::InternalError(
          "Decompression failed due to not all input "
          "consumed");
    }
  }
  if (r != Z_STREAM_END) {
    VLOG(2) << "zlib: Data error";
    return absl::InternalError("Decompression failed due to data error");
  }

  // TODO(vigneshbabu): Add a Truncate() method to the Slice type to avoid using
  // the underlying C type here.
  grpc_slice slice = outbuf.TakeCSlice();
  if (slice.refcount) {
    slice.data.refcounted.length -= zs->avail_out;
  } else {
    slice.data.inlined.length -= zs->avail_out;
  }
  output_sb.AppendIndexed(Slice(slice));
  return output_sb;
}

void* ZallocGpr(void* /*opaque*/, unsigned int items, unsigned int size) {
  return gpr_malloc(items * size);
}

void ZFreeGpr(void* /*opaque*/, void* address) { gpr_free(address); }

std::optional<SliceBuffer> ZlibCompress(const SliceBuffer& input, int gzip) {
  z_stream zs;
  absl::StatusOr<SliceBuffer> compression_result;
  memset(&zs, 0, sizeof(zs));
  zs.zalloc = ZallocGpr;
  zs.zfree = ZFreeGpr;
  bool r = deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                        15 | (gzip ? 16 : 0), 8, Z_DEFAULT_STRATEGY);
  GRPC_CHECK(r == Z_OK);
  compression_result = ZlibBody(&zs, input, deflate, input.Length());
  deflateEnd(&zs);
  if (compression_result.ok() &&
      compression_result->Length() < input.Length()) {
    return std::move(*compression_result);
  }
  return std::nullopt;
}

absl::StatusOr<SliceBuffer> ZlibDecompress(
    const SliceBuffer& input, int gzip,
    std::optional<uint32_t> max_output_size) {
  z_stream zs;
  absl::StatusOr<SliceBuffer> decompression_result;
  memset(&zs, 0, sizeof(zs));
  zs.zalloc = ZallocGpr;
  zs.zfree = ZFreeGpr;
  bool r = inflateInit2(&zs, 15 | (gzip ? 16 : 0));
  GRPC_CHECK(r == Z_OK);
  decompression_result = ZlibBody(&zs, input, inflate, max_output_size);
  inflateEnd(&zs);
  return decompression_result;
}

}  // namespace

std::optional<SliceBuffer> MessageCompress(grpc_compression_algorithm algorithm,
                                           const SliceBuffer& input) {
  switch (algorithm) {
    case GRPC_COMPRESS_NONE:
      // the fallback path always needs to be send uncompressed: we simply
      // rely on that here
      return std::nullopt;
    case GRPC_COMPRESS_DEFLATE:
      return ZlibCompress(input, 0);
    case GRPC_COMPRESS_GZIP:
      return ZlibCompress(input, 1);
    case GRPC_COMPRESS_ALGORITHMS_COUNT:
      break;
  }
  LOG(ERROR) << "invalid compression algorithm " << algorithm;
  return std::nullopt;
}

absl::StatusOr<SliceBuffer> MessageDecompress(
    grpc_compression_algorithm algorithm, const SliceBuffer& input,
    std::optional<uint32_t> max_output_size) {
  switch (algorithm) {
    case GRPC_COMPRESS_NONE: {
      SliceBuffer output;
      output.Append(input);
      return output;
    }
    case GRPC_COMPRESS_DEFLATE:
      return ZlibDecompress(input, 0, max_output_size);
    case GRPC_COMPRESS_GZIP:
      return ZlibDecompress(input, 1, max_output_size);
    case GRPC_COMPRESS_ALGORITHMS_COUNT:
      break;
  }
  LOG(ERROR) << "invalid compression algorithm " << algorithm;
  return absl::InternalError("Invalid compression algorithm");
}

}  // namespace grpc_core

int grpc_msg_compress(grpc_compression_algorithm algorithm,
                      grpc_slice_buffer* input, grpc_slice_buffer* output) {
  int retval = 1;
  grpc_core::SliceBuffer input_sb;
  grpc_slice_buffer_swap(input, input_sb.c_slice_buffer());
  std::optional<grpc_core::SliceBuffer> output_sb =
      grpc_core::MessageCompress(algorithm, input_sb);
  if (!output_sb.has_value()) {
    output_sb.emplace();
    output_sb->Append(input_sb);
    retval = 0;
  }
  grpc_slice_buffer_swap(input, input_sb.c_slice_buffer());
  grpc_slice_buffer_swap(output, output_sb->c_slice_buffer());
  return retval;
}

int grpc_msg_decompress(grpc_compression_algorithm algorithm,
                        grpc_slice_buffer* input, grpc_slice_buffer* output) {
  grpc_core::SliceBuffer input_sb;
  grpc_slice_buffer_swap(input, input_sb.c_slice_buffer());

  absl::StatusOr<grpc_core::SliceBuffer> output_sb =
      grpc_core::MessageDecompress(algorithm, input_sb, std::nullopt);
  if (!output_sb.ok()) {
    return 0;
  }
  grpc_slice_buffer_swap(input, input_sb.c_slice_buffer());
  grpc_slice_buffer_swap(output, output_sb->c_slice_buffer());
  return 1;
}
