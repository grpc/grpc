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

#include <grpc/support/port_platform.h>

#include "src/core/lib/compression/message_compress.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include <zlib.h>

#include "snappy-sinksource.h"
#include "snappy.h"

#include "src/core/lib/slice/slice_internal.h"

#define OUTPUT_BLOCK_SIZE 1024

static int zlib_body(z_stream* zs, grpc_slice_buffer* input,
                     grpc_slice_buffer* output,
                     int (*flate)(z_stream* zs, int flush)) {
  int r = Z_STREAM_END; /* Do not fail on an empty input. */
  int flush;
  size_t i;
  grpc_slice outbuf = GRPC_SLICE_MALLOC(OUTPUT_BLOCK_SIZE);
  const uInt uint_max = ~static_cast<uInt>(0);

  GPR_ASSERT(GRPC_SLICE_LENGTH(outbuf) <= uint_max);
  zs->avail_out = static_cast<uInt> GRPC_SLICE_LENGTH(outbuf);
  zs->next_out = GRPC_SLICE_START_PTR(outbuf);
  flush = Z_NO_FLUSH;
  for (i = 0; i < input->count; i++) {
    if (i == input->count - 1) flush = Z_FINISH;
    GPR_ASSERT(GRPC_SLICE_LENGTH(input->slices[i]) <= uint_max);
    zs->avail_in = static_cast<uInt> GRPC_SLICE_LENGTH(input->slices[i]);
    zs->next_in = GRPC_SLICE_START_PTR(input->slices[i]);
    do {
      if (zs->avail_out == 0) {
        grpc_slice_buffer_add_indexed(output, outbuf);
        outbuf = GRPC_SLICE_MALLOC(OUTPUT_BLOCK_SIZE);
        GPR_ASSERT(GRPC_SLICE_LENGTH(outbuf) <= uint_max);
        zs->avail_out = static_cast<uInt> GRPC_SLICE_LENGTH(outbuf);
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
  if (r != Z_STREAM_END) {
    gpr_log(GPR_INFO, "zlib: Data error");
    goto error;
  }

  GPR_ASSERT(outbuf.refcount);
  outbuf.data.refcounted.length -= zs->avail_out;
  grpc_slice_buffer_add_indexed(output, outbuf);

  return 1;

error:
  grpc_slice_unref_internal(outbuf);
  return 0;
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
  GPR_ASSERT(r == Z_OK);
  r = zlib_body(&zs, input, output, deflate) && output->length < input->length;
  if (!r) {
    for (i = count_before; i < output->count; i++) {
      grpc_slice_unref_internal(output->slices[i]);
    }
    output->count = count_before;
    output->length = length_before;
  }
  deflateEnd(&zs);
  return r;
}

static int zlib_decompress(grpc_slice_buffer* input, grpc_slice_buffer* output,
                           int gzip) {
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
  r = zlib_body(&zs, input, output, inflate);
  if (!r) {
    for (i = count_before; i < output->count; i++) {
      grpc_slice_unref_internal(output->slices[i]);
    }
    output->count = count_before;
    output->length = length_before;
  }
  inflateEnd(&zs);
  return r;
}

// Snappy support
//
// SliceBufferSource and SliceBufferSink are adaptors so we can pass
// grpc_slice_buffer data to snappy's Compress and Uncompress functions, using
// the Source/Sink interface.

class SliceBufferSource : public ::snappy::Source {
 private:
  // original buffer
  grpc_slice_buffer* input_;
  size_t total_remaining_;
  size_t current_slice_index_;
  size_t current_slice_pos_;

 public:
  explicit SliceBufferSource(grpc_slice_buffer* input) : input_(input) {
    this->total_remaining_ = input->length;
    this->current_slice_index_ = 0;
    this->current_slice_pos_ = 0;
  }

  // Return the number of bytes left to read from the source
  size_t Available() const override { return total_remaining_; }

  // Peek at the next flat region of the source.  Does not reposition
  // the source.  The returned region is empty iff Available()==0.
  const char* Peek(size_t* len) override {
    if (total_remaining_ <= 0) {
      *len = 0;
      return nullptr;
    }

    *len = GRPC_SLICE_LENGTH(input_->slices[current_slice_index_]) -
           current_slice_pos_;

    return reinterpret_cast<char*>(
        GRPC_SLICE_START_PTR(input_->slices[current_slice_index_]) +
        current_slice_pos_);
  }

  // Skip the next n bytes.  Invalidates any buffer returned by
  // a previous call to Peek().
  void Skip(size_t n) override {
    while (n > 0) {
      const size_t current_slice_length =
          GRPC_SLICE_LENGTH(input_->slices[current_slice_index_]);

      // calculate how much of this current slice to skip forward
      size_t skip_this_slice = std::min(n, current_slice_length);

      current_slice_pos_ += skip_this_slice;
      n -= skip_this_slice;
      total_remaining_ -= skip_this_slice;

      // check if we should move to next slice
      if (this->current_slice_pos_ >= current_slice_length) {
        ++current_slice_index_;
        current_slice_pos_ = 0;
      }
    }
  }
};

class SliceBufferSink : public ::snappy::Sink {
 public:
  explicit SliceBufferSink(grpc_slice_buffer* output) : output_(output) {}

  void Append(const char* bytes, size_t n) override {
    grpc_slice_buffer_add(output_, grpc_slice_from_copied_buffer(bytes, n));
  }

 private:
  grpc_slice_buffer* output_;
};

static int snappy_compress(grpc_slice_buffer* input,
                           grpc_slice_buffer* output) {
  SliceBufferSource source(input);
  SliceBufferSink sink(output);

  size_t bytes_written = snappy::Compress(&source, &sink);

  // if the compressed output is larger than the input, return 0 to signal that
  // it should not be used. The calling code will copy the input over to the
  // output in that case, but we have to reset output first - as
  // snappy::Compress will have written to it.
  if (bytes_written < input->length) {
    return 1;
  } else {
    grpc_slice_buffer_reset_and_unref(output);
    return 0;
  }
}

static int snappy_decompress(grpc_slice_buffer* input,
                             grpc_slice_buffer* output) {
  SliceBufferSource source(input);
  SliceBufferSink sink(output);

  bool r = snappy::Uncompress(&source, &sink);
  return r;
}

static int copy(grpc_slice_buffer* input, grpc_slice_buffer* output) {
  size_t i;
  for (i = 0; i < input->count; i++) {
    grpc_slice_buffer_add(output, grpc_slice_ref_internal(input->slices[i]));
  }
  return 1;
}

static int compress_inner(grpc_message_compression_algorithm algorithm,
                          grpc_slice_buffer* input, grpc_slice_buffer* output) {
  switch (algorithm) {
    case GRPC_MESSAGE_COMPRESS_NONE:
      /* the fallback path always needs to be send uncompressed: we simply
         rely on that here */
      return 0;
    case GRPC_MESSAGE_COMPRESS_DEFLATE:
      return zlib_compress(input, output, 0);
    case GRPC_MESSAGE_COMPRESS_GZIP:
      return zlib_compress(input, output, 1);
    case GRPC_MESSAGE_COMPRESS_SNAPPY:
      return snappy_compress(input, output);
    case GRPC_MESSAGE_COMPRESS_ALGORITHMS_COUNT:
      break;
  }
  gpr_log(GPR_ERROR, "invalid compression algorithm %d", algorithm);
  return 0;
}

int grpc_msg_compress(grpc_message_compression_algorithm algorithm,
                      grpc_slice_buffer* input, grpc_slice_buffer* output) {
  if (!compress_inner(algorithm, input, output)) {
    copy(input, output);
    return 0;
  }
  return 1;
}

int grpc_msg_decompress(grpc_message_compression_algorithm algorithm,
                        grpc_slice_buffer* input, grpc_slice_buffer* output) {
  switch (algorithm) {
    case GRPC_MESSAGE_COMPRESS_NONE:
      return copy(input, output);
    case GRPC_MESSAGE_COMPRESS_DEFLATE:
      return zlib_decompress(input, output, 0);
    case GRPC_MESSAGE_COMPRESS_GZIP:
      return zlib_decompress(input, output, 1);
    case GRPC_MESSAGE_COMPRESS_SNAPPY:
      return snappy_decompress(input, output);
    case GRPC_MESSAGE_COMPRESS_ALGORITHMS_COUNT:
      break;
  }
  gpr_log(GPR_ERROR, "invalid compression algorithm %d", algorithm);
  return 0;
}
