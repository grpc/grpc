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

#include <stdlib.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/lib/support/murmur_hash.h"
#include "test/core/util/slice_splitter.h"
#include "test/core/util/test_config.h"

typedef enum { ONE_A = 0, ONE_KB_A, ONE_MB_A, TEST_VALUE_COUNT } test_value;

typedef enum {
  SHOULD_NOT_COMPRESS,
  SHOULD_COMPRESS,
  MAYBE_COMPRESSES
} compressability;

static void assert_passthrough(gpr_slice value,
                               grpc_compression_algorithm algorithm,
                               grpc_slice_split_mode uncompressed_split_mode,
                               grpc_slice_split_mode compressed_split_mode,
                               compressability compress_result_check) {
  gpr_slice_buffer input;
  gpr_slice_buffer compressed_raw;
  gpr_slice_buffer compressed;
  gpr_slice_buffer output;
  gpr_slice final;
  int was_compressed;
  char *algorithm_name;

  GPR_ASSERT(grpc_compression_algorithm_name(algorithm, &algorithm_name) != 0);
  gpr_log(GPR_INFO,
          "assert_passthrough: value_length=%d value_hash=0x%08x "
          "algorithm='%s' uncompressed_split='%s' compressed_split='%s'",
          GPR_SLICE_LENGTH(value), gpr_murmur_hash3(GPR_SLICE_START_PTR(value),
                                                    GPR_SLICE_LENGTH(value), 0),
          algorithm_name, grpc_slice_split_mode_name(uncompressed_split_mode),
          grpc_slice_split_mode_name(compressed_split_mode));

  gpr_slice_buffer_init(&input);
  gpr_slice_buffer_init(&compressed_raw);
  gpr_slice_buffer_init(&compressed);
  gpr_slice_buffer_init(&output);

  grpc_split_slices_to_buffer(uncompressed_split_mode, &value, 1, &input);

  was_compressed = grpc_msg_compress(algorithm, &input, &compressed_raw);
  GPR_ASSERT(input.count > 0);

  switch (compress_result_check) {
    case SHOULD_NOT_COMPRESS:
      GPR_ASSERT(was_compressed == 0);
      break;
    case SHOULD_COMPRESS:
      GPR_ASSERT(was_compressed == 1);
      break;
    case MAYBE_COMPRESSES:
      /* no check */
      break;
  }

  grpc_split_slice_buffer(compressed_split_mode, &compressed_raw, &compressed);

  GPR_ASSERT(grpc_msg_decompress(
      was_compressed ? algorithm : GRPC_COMPRESS_NONE, &compressed, &output));

  final = grpc_slice_merge(output.slices, output.count);
  GPR_ASSERT(0 == gpr_slice_cmp(value, final));

  gpr_slice_buffer_destroy(&input);
  gpr_slice_buffer_destroy(&compressed);
  gpr_slice_buffer_destroy(&compressed_raw);
  gpr_slice_buffer_destroy(&output);
  gpr_slice_unref(final);
}

static gpr_slice repeated(char c, size_t length) {
  gpr_slice out = gpr_slice_malloc(length);
  memset(GPR_SLICE_START_PTR(out), c, length);
  return out;
}

static compressability get_compressability(
    test_value id, grpc_compression_algorithm algorithm) {
  if (algorithm == GRPC_COMPRESS_NONE) return SHOULD_NOT_COMPRESS;
  switch (id) {
    case ONE_A:
      return SHOULD_NOT_COMPRESS;
    case ONE_KB_A:
    case ONE_MB_A:
      return SHOULD_COMPRESS;
    case TEST_VALUE_COUNT:
      abort();
      break;
  }
  return MAYBE_COMPRESSES;
}

static gpr_slice create_test_value(test_value id) {
  switch (id) {
    case ONE_A:
      return gpr_slice_from_copied_string("a");
    case ONE_KB_A:
      return repeated('a', 1024);
    case ONE_MB_A:
      return repeated('a', 1024 * 1024);
    case TEST_VALUE_COUNT:
      abort();
      break;
  }
  return gpr_slice_from_copied_string("bad value");
}

static void test_tiny_data_compress(void) {
  gpr_slice_buffer input;
  gpr_slice_buffer output;
  grpc_compression_algorithm i;

  gpr_slice_buffer_init(&input);
  gpr_slice_buffer_init(&output);
  gpr_slice_buffer_add(&input, create_test_value(ONE_A));

  for (i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    if (i == GRPC_COMPRESS_NONE) continue;
    GPR_ASSERT(0 == grpc_msg_compress(i, &input, &output));
    GPR_ASSERT(1 == output.count);
  }

  gpr_slice_buffer_destroy(&input);
  gpr_slice_buffer_destroy(&output);
}

static void test_bad_decompression_data_crc(void) {
  gpr_slice_buffer input;
  gpr_slice_buffer corrupted;
  gpr_slice_buffer output;
  size_t idx;
  const uint32_t bad = 0xdeadbeef;

  gpr_slice_buffer_init(&input);
  gpr_slice_buffer_init(&corrupted);
  gpr_slice_buffer_init(&output);
  gpr_slice_buffer_add(&input, create_test_value(ONE_MB_A));

  /* compress it */
  grpc_msg_compress(GRPC_COMPRESS_GZIP, &input, &corrupted);
  /* corrupt the output by smashing the CRC */
  GPR_ASSERT(corrupted.count > 1);
  GPR_ASSERT(GPR_SLICE_LENGTH(corrupted.slices[1]) > 8);
  idx = GPR_SLICE_LENGTH(corrupted.slices[1]) - 8;
  memcpy(GPR_SLICE_START_PTR(corrupted.slices[1]) + idx, &bad, 4);

  /* try (and fail) to decompress the corrupted compresed buffer */
  GPR_ASSERT(0 == grpc_msg_decompress(GRPC_COMPRESS_GZIP, &corrupted, &output));

  gpr_slice_buffer_destroy(&input);
  gpr_slice_buffer_destroy(&corrupted);
  gpr_slice_buffer_destroy(&output);
}

static void test_bad_decompression_data_trailing_garbage(void) {
  gpr_slice_buffer input;
  gpr_slice_buffer output;

  gpr_slice_buffer_init(&input);
  gpr_slice_buffer_init(&output);
  /* append 0x99 to the end of an otherwise valid stream */
  gpr_slice_buffer_add(
      &input, gpr_slice_from_copied_buffer(
                  "\x78\xda\x63\x60\x60\x60\x00\x00\x00\x04\x00\x01\x99", 13));

  /* try (and fail) to decompress the invalid compresed buffer */
  GPR_ASSERT(0 == grpc_msg_decompress(GRPC_COMPRESS_DEFLATE, &input, &output));

  gpr_slice_buffer_destroy(&input);
  gpr_slice_buffer_destroy(&output);
}

static void test_bad_decompression_data_stream(void) {
  gpr_slice_buffer input;
  gpr_slice_buffer output;

  gpr_slice_buffer_init(&input);
  gpr_slice_buffer_init(&output);
  gpr_slice_buffer_add(&input,
                       gpr_slice_from_copied_buffer("\x78\xda\xff\xff", 4));

  /* try (and fail) to decompress the invalid compresed buffer */
  GPR_ASSERT(0 == grpc_msg_decompress(GRPC_COMPRESS_DEFLATE, &input, &output));

  gpr_slice_buffer_destroy(&input);
  gpr_slice_buffer_destroy(&output);
}

static void test_bad_compression_algorithm(void) {
  gpr_slice_buffer input;
  gpr_slice_buffer output;
  int was_compressed;

  gpr_slice_buffer_init(&input);
  gpr_slice_buffer_init(&output);
  gpr_slice_buffer_add(&input,
                       gpr_slice_from_copied_string("Never gonna give you up"));
  was_compressed =
      grpc_msg_compress(GRPC_COMPRESS_ALGORITHMS_COUNT, &input, &output);
  GPR_ASSERT(0 == was_compressed);

  was_compressed =
      grpc_msg_compress(GRPC_COMPRESS_ALGORITHMS_COUNT + 123, &input, &output);
  GPR_ASSERT(0 == was_compressed);

  gpr_slice_buffer_destroy(&input);
  gpr_slice_buffer_destroy(&output);
}

static void test_bad_decompression_algorithm(void) {
  gpr_slice_buffer input;
  gpr_slice_buffer output;
  int was_decompressed;

  gpr_slice_buffer_init(&input);
  gpr_slice_buffer_init(&output);
  gpr_slice_buffer_add(&input,
                       gpr_slice_from_copied_string(
                           "I'm not really compressed but it doesn't matter"));
  was_decompressed =
      grpc_msg_decompress(GRPC_COMPRESS_ALGORITHMS_COUNT, &input, &output);
  GPR_ASSERT(0 == was_decompressed);

  was_decompressed = grpc_msg_decompress(GRPC_COMPRESS_ALGORITHMS_COUNT + 123,
                                         &input, &output);
  GPR_ASSERT(0 == was_decompressed);

  gpr_slice_buffer_destroy(&input);
  gpr_slice_buffer_destroy(&output);
}

int main(int argc, char **argv) {
  unsigned i, j, k, m;
  grpc_slice_split_mode uncompressed_split_modes[] = {
      GRPC_SLICE_SPLIT_IDENTITY, GRPC_SLICE_SPLIT_ONE_BYTE};
  grpc_slice_split_mode compressed_split_modes[] = {GRPC_SLICE_SPLIT_MERGE_ALL,
                                                    GRPC_SLICE_SPLIT_IDENTITY,
                                                    GRPC_SLICE_SPLIT_ONE_BYTE};

  grpc_test_init(argc, argv);
  grpc_init();

  for (i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    for (j = 0; j < GPR_ARRAY_SIZE(uncompressed_split_modes); j++) {
      for (k = 0; k < GPR_ARRAY_SIZE(compressed_split_modes); k++) {
        for (m = 0; m < TEST_VALUE_COUNT; m++) {
          gpr_slice slice = create_test_value(m);
          assert_passthrough(slice, i, j, k, get_compressability(m, i));
          gpr_slice_unref(slice);
        }
      }
    }
  }

  test_tiny_data_compress();
  test_bad_decompression_data_crc();
  test_bad_decompression_data_stream();
  test_bad_decompression_data_trailing_garbage();
  test_bad_compression_algorithm();
  test_bad_decompression_algorithm();
  grpc_shutdown();

  return 0;
}
