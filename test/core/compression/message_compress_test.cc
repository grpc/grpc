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

#include <grpc/compression.h>
#include <grpc/slice_buffer.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <memory>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/useful.h"
#include "test/core/test_util/slice_splitter.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"

typedef enum { ONE_A = 0, ONE_KB_A, ONE_MB_A, TEST_VALUE_COUNT } test_value;

typedef enum {
  SHOULD_NOT_COMPRESS,
  SHOULD_COMPRESS,
  MAYBE_COMPRESSES
} compressability;

static void assert_passthrough(grpc_slice value,
                               grpc_compression_algorithm algorithm,
                               grpc_slice_split_mode uncompressed_split_mode,
                               grpc_slice_split_mode compressed_split_mode,
                               compressability compress_result_check) {
  grpc_slice_buffer input;
  grpc_slice_buffer compressed_raw;
  grpc_slice_buffer compressed;
  grpc_slice_buffer output;
  grpc_slice final;
  int was_compressed;
  const char* algorithm_name;

  ASSERT_NE(grpc_compression_algorithm_name(algorithm, &algorithm_name), 0);
  LOG(INFO) << "assert_passthrough: value_length=" << GRPC_SLICE_LENGTH(value)
            << " algorithm='" << algorithm_name << "' uncompressed_split='"
            << grpc_slice_split_mode_name(uncompressed_split_mode)
            << "' compressed_split='"
            << grpc_slice_split_mode_name(compressed_split_mode) << "'";

  grpc_slice_buffer_init(&input);
  grpc_slice_buffer_init(&compressed_raw);
  grpc_slice_buffer_init(&compressed);
  grpc_slice_buffer_init(&output);

  grpc_split_slices_to_buffer(uncompressed_split_mode, &value, 1, &input);

  {
    grpc_core::ExecCtx exec_ctx;
    was_compressed = grpc_msg_compress(algorithm, &input, &compressed_raw);
  }
  ASSERT_GT(input.count, 0);

  switch (compress_result_check) {
    case SHOULD_NOT_COMPRESS:
      ASSERT_EQ(was_compressed, 0);
      break;
    case SHOULD_COMPRESS:
      ASSERT_EQ(was_compressed, 1);
      break;
    case MAYBE_COMPRESSES:
      // no check
      break;
  }

  grpc_split_slice_buffer(compressed_split_mode, &compressed_raw, &compressed);

  {
    grpc_core::ExecCtx exec_ctx;
    ASSERT_TRUE(grpc_msg_decompress(
        was_compressed ? algorithm : GRPC_COMPRESS_NONE, &compressed, &output));
  }

  final = grpc_slice_merge(output.slices, output.count);
  ASSERT_TRUE(grpc_slice_eq(value, final));

  grpc_slice_buffer_destroy(&input);
  grpc_slice_buffer_destroy(&compressed);
  grpc_slice_buffer_destroy(&compressed_raw);
  grpc_slice_buffer_destroy(&output);
  grpc_slice_unref(final);
}

static grpc_slice repeated(char c, size_t length) {
  grpc_slice out = grpc_slice_malloc(length);
  memset(GRPC_SLICE_START_PTR(out), c, length);
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
  }
  return MAYBE_COMPRESSES;
}

static grpc_slice create_test_value(test_value id) {
  switch (id) {
    case ONE_A:
      return grpc_slice_from_copied_string("a");
    case ONE_KB_A:
      return repeated('a', 1024);
    case ONE_MB_A:
      return repeated('a', 1024 * 1024);
    case TEST_VALUE_COUNT:
      abort();
  }
  return grpc_slice_from_copied_string("bad value");
}

TEST(MessageCompressTest, TinyDataCompress) {
  grpc_core::SliceBuffer input;

  input.Append(grpc_core::Slice(create_test_value(ONE_A)));

  for (int i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    if (i == GRPC_COMPRESS_NONE) continue;
    grpc_core::ExecCtx exec_ctx;
    auto res = grpc_core::MessageCompress(
        static_cast<grpc_compression_algorithm>(i), input);
    ASSERT_FALSE(res.has_value());
  }
}

TEST(MessageCompressTest, MaxOutputSizeExceeded) {
  grpc_core::SliceBuffer input;

  input.Append(grpc_core::Slice(create_test_value(ONE_KB_A)));

  grpc_core::ExecCtx exec_ctx;

  auto compressed = grpc_core::MessageCompress(GRPC_COMPRESS_GZIP, input);
  ASSERT_TRUE(compressed.has_value());

  // The allowed max output size is only 10 bytes which is not enough to hold
  // the decompressed data. So we expect a ResourceExhausted error.
  auto decompressed =
      grpc_core::MessageDecompress(GRPC_COMPRESS_GZIP, *compressed, 10);
  EXPECT_EQ(decompressed.status(), absl::ResourceExhaustedError(
                                       "Decompressed message larger than max"));
}

TEST(MessageCompressTest, BadDecompressionDataCrc) {
  grpc_core::SliceBuffer input;
  size_t idx;
  const uint32_t bad = 0xdeadbeef;

  input.Append(grpc_core::Slice(create_test_value(ONE_MB_A)));

  grpc_core::ExecCtx exec_ctx;
  // compress it
  auto compressed = grpc_core::MessageCompress(GRPC_COMPRESS_GZIP, input);
  ASSERT_TRUE(compressed.has_value());
  // corrupt the output by smashing the CRC
  ASSERT_GT(compressed->Count(), 1);
  ASSERT_GT((*compressed)[1].size(), 8);
  idx = (*compressed)[1].size() - 8;
  memcpy(const_cast<uint8_t*>((*compressed)[1].data() + idx), &bad, 4);

  // try (and fail) to decompress the corrupted compressed buffer
  auto decompressed = grpc_core::MessageDecompress(GRPC_COMPRESS_GZIP,
                                                   *compressed, std::nullopt);
  EXPECT_EQ(decompressed.status(),
            absl::InternalError("Decompression failed due to zlib error"));
}

TEST(MessageCompressTest, BadDecompressionDataMissingTrailer) {
  grpc_core::SliceBuffer input;
  grpc_core::SliceBuffer garbage;

  input.Append(grpc_core::Slice(create_test_value(ONE_MB_A)));

  grpc_core::ExecCtx exec_ctx;
  // compress it
  auto compressed = grpc_core::MessageCompress(GRPC_COMPRESS_GZIP, input);
  ASSERT_TRUE(compressed.has_value());
  ASSERT_GT(compressed->Length(), 8);
  // Remove the footer from the compressed message
  compressed->MoveLastNBytesIntoSliceBuffer(8, garbage);
  // try (and fail) to decompress the compressed buffer without the footer
  auto decompressed = grpc_core::MessageDecompress(GRPC_COMPRESS_GZIP,
                                                   *compressed, std::nullopt);
  EXPECT_EQ(decompressed.status(),
            absl::InternalError("Decompression failed due to data error"));
}

TEST(MessageCompressTest, BadDecompressionDataTrailingGarbage) {
  grpc_core::SliceBuffer input;

  input.Append(grpc_core::Slice::FromCopiedBuffer(
      "\x78\xda\x63\x60\x60\x60\x00\x00\x00\x04\x00\x01\x99", 13));

  grpc_core::ExecCtx exec_ctx;
  auto decompressed =
      grpc_core::MessageDecompress(GRPC_COMPRESS_DEFLATE, input, std::nullopt);
  EXPECT_EQ(decompressed.status(),
            absl::InternalError("Decompression failed due to not all input "
                                "consumed"));
}

TEST(MessageCompressTest, BadDecompressionDataStream) {
  grpc_core::SliceBuffer input;

  input.Append(grpc_core::Slice::FromCopiedBuffer("\x78\xda\xff\xff", 4));

  grpc_core::ExecCtx exec_ctx;
  auto decompressed =
      grpc_core::MessageDecompress(GRPC_COMPRESS_DEFLATE, input, std::nullopt);
  EXPECT_EQ(decompressed.status(),
            absl::InternalError("Decompression failed due to zlib error"));
}

TEST(MessageCompressTest, BadCompressionAlgorithm) {
  grpc_core::SliceBuffer input;

  input.Append(grpc_core::Slice::FromCopiedString("Never gonna give you up"));

  grpc_core::ExecCtx exec_ctx;
  ASSERT_FALSE(grpc_core::MessageCompress(GRPC_COMPRESS_ALGORITHMS_COUNT, input)
                   .has_value());

  ASSERT_FALSE(
      grpc_core::MessageCompress(static_cast<grpc_compression_algorithm>(
                                     GRPC_COMPRESS_ALGORITHMS_COUNT + 123),
                                 input)
          .has_value());
}

TEST(MessageCompressTest, BadDecompressionAlgorithm) {
  grpc_core::SliceBuffer input;

  input.Append(grpc_core::Slice::FromCopiedString(
      "I'm not really compressed but it doesn't matter"));

  grpc_core::ExecCtx exec_ctx;
  auto decompressed1 = grpc_core::MessageDecompress(
      GRPC_COMPRESS_ALGORITHMS_COUNT, input, std::nullopt);
  EXPECT_EQ(decompressed1.status(),
            absl::InternalError("Invalid compression algorithm"));

  auto decompressed2 =
      grpc_core::MessageDecompress(static_cast<grpc_compression_algorithm>(
                                       GRPC_COMPRESS_ALGORITHMS_COUNT + 123),
                                   input, std::nullopt);
  EXPECT_EQ(decompressed2.status(),
            absl::InternalError("Invalid compression algorithm"));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;

  unsigned i, j, k, m;
  grpc_slice_split_mode uncompressed_split_modes[] = {
      GRPC_SLICE_SPLIT_IDENTITY, GRPC_SLICE_SPLIT_ONE_BYTE};
  grpc_slice_split_mode compressed_split_modes[] = {GRPC_SLICE_SPLIT_MERGE_ALL,
                                                    GRPC_SLICE_SPLIT_IDENTITY,
                                                    GRPC_SLICE_SPLIT_ONE_BYTE};
  for (i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    for (j = 0; j < GPR_ARRAY_SIZE(uncompressed_split_modes); j++) {
      for (k = 0; k < GPR_ARRAY_SIZE(compressed_split_modes); k++) {
        for (m = 0; m < TEST_VALUE_COUNT; m++) {
          grpc_slice slice = create_test_value(static_cast<test_value>(m));
          assert_passthrough(
              slice, static_cast<grpc_compression_algorithm>(i),
              static_cast<grpc_slice_split_mode>(j),
              static_cast<grpc_slice_split_mode>(k),
              get_compressability(static_cast<test_value>(m),
                                  static_cast<grpc_compression_algorithm>(i)));
          grpc_slice_unref(slice);
        }
      }
    }
  }

  return RUN_ALL_TESTS();
}
