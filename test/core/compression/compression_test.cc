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

#include <stdint.h>
#include <stdlib.h>

#include <memory>

#include "gtest/gtest.h"

#include <grpc/compression.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"
#include "test/core/util/test_config.h"

TEST(CompressionTest, CompressionAlgorithmParse) {
  size_t i;
  const char* valid_names[] = {"identity", "gzip", "deflate"};
  const grpc_compression_algorithm valid_algorithms[] = {
      GRPC_COMPRESS_NONE,
      GRPC_COMPRESS_GZIP,
      GRPC_COMPRESS_DEFLATE,
  };
  const char* invalid_names[] = {"gzip2", "foo", "", "2gzip"};

  gpr_log(GPR_DEBUG, "test_compression_algorithm_parse");

  for (i = 0; i < GPR_ARRAY_SIZE(valid_names); i++) {
    const char* valid_name = valid_names[i];
    grpc_compression_algorithm algorithm;
    const int success = grpc_compression_algorithm_parse(
        grpc_slice_from_static_string(valid_name), &algorithm);
    ASSERT_NE(success, 0);
    ASSERT_EQ(algorithm, valid_algorithms[i]);
  }

  for (i = 0; i < GPR_ARRAY_SIZE(invalid_names); i++) {
    const char* invalid_name = invalid_names[i];
    grpc_compression_algorithm algorithm;
    int success;
    success = grpc_compression_algorithm_parse(
        grpc_slice_from_static_string(invalid_name), &algorithm);
    ASSERT_EQ(success, 0);
    // the value of "algorithm" is undefined upon failure
  }
}

TEST(CompressionTest, CompressionAlgorithmName) {
  int success;
  const char* name;
  size_t i;
  const char* valid_names[] = {"identity", "gzip", "deflate"};
  const grpc_compression_algorithm valid_algorithms[] = {
      GRPC_COMPRESS_NONE,
      GRPC_COMPRESS_GZIP,
      GRPC_COMPRESS_DEFLATE,
  };

  gpr_log(GPR_DEBUG, "test_compression_algorithm_name");

  for (i = 0; i < GPR_ARRAY_SIZE(valid_algorithms); i++) {
    success = grpc_compression_algorithm_name(valid_algorithms[i], &name);
    ASSERT_NE(success, 0);
    ASSERT_STREQ(name, valid_names[i]);
  }

  success =
      grpc_compression_algorithm_name(GRPC_COMPRESS_ALGORITHMS_COUNT, &name);
  ASSERT_EQ(success, 0);
  // the value of "name" is undefined upon failure
}

TEST(CompressionTest, CompressionAlgorithmForLevel) {
  gpr_log(GPR_DEBUG, "test_compression_algorithm_for_level");

  {
    // accept only identity (aka none)
    uint32_t accepted_encodings = 0;
    grpc_core::SetBit(&accepted_encodings, GRPC_COMPRESS_NONE);  // always

    ASSERT_EQ(GRPC_COMPRESS_NONE,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_NONE,
                                                   accepted_encodings));

    ASSERT_EQ(GRPC_COMPRESS_NONE,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_LOW,
                                                   accepted_encodings));

    ASSERT_EQ(GRPC_COMPRESS_NONE,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_MED,
                                                   accepted_encodings));

    ASSERT_EQ(GRPC_COMPRESS_NONE,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_HIGH,
                                                   accepted_encodings));
  }

  {
    // accept only gzip
    uint32_t accepted_encodings = 0;
    grpc_core::SetBit(&accepted_encodings, GRPC_COMPRESS_NONE);  // always
    grpc_core::SetBit(&accepted_encodings, GRPC_COMPRESS_GZIP);

    ASSERT_EQ(GRPC_COMPRESS_NONE,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_NONE,
                                                   accepted_encodings));

    ASSERT_EQ(GRPC_COMPRESS_GZIP,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_LOW,
                                                   accepted_encodings));

    ASSERT_EQ(GRPC_COMPRESS_GZIP,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_MED,
                                                   accepted_encodings));

    ASSERT_EQ(GRPC_COMPRESS_GZIP,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_HIGH,
                                                   accepted_encodings));
  }

  {
    // accept only deflate
    uint32_t accepted_encodings = 0;
    grpc_core::SetBit(&accepted_encodings, GRPC_COMPRESS_NONE);  // always
    grpc_core::SetBit(&accepted_encodings, GRPC_COMPRESS_DEFLATE);

    ASSERT_EQ(GRPC_COMPRESS_NONE,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_NONE,
                                                   accepted_encodings));

    ASSERT_EQ(GRPC_COMPRESS_DEFLATE,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_LOW,
                                                   accepted_encodings));

    ASSERT_EQ(GRPC_COMPRESS_DEFLATE,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_MED,
                                                   accepted_encodings));

    ASSERT_EQ(GRPC_COMPRESS_DEFLATE,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_HIGH,
                                                   accepted_encodings));
  }

  {
    // accept gzip and deflate
    uint32_t accepted_encodings = 0;
    grpc_core::SetBit(&accepted_encodings, GRPC_COMPRESS_NONE);  // always
    grpc_core::SetBit(&accepted_encodings, GRPC_COMPRESS_GZIP);
    grpc_core::SetBit(&accepted_encodings, GRPC_COMPRESS_DEFLATE);

    ASSERT_EQ(GRPC_COMPRESS_NONE,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_NONE,
                                                   accepted_encodings));

    ASSERT_EQ(GRPC_COMPRESS_GZIP,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_LOW,
                                                   accepted_encodings));

    ASSERT_EQ(GRPC_COMPRESS_DEFLATE,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_MED,
                                                   accepted_encodings));

    ASSERT_EQ(GRPC_COMPRESS_DEFLATE,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_HIGH,
                                                   accepted_encodings));
  }

  {
    // accept all algorithms
    uint32_t accepted_encodings = 0;
    grpc_core::SetBit(&accepted_encodings, GRPC_COMPRESS_NONE);  // always
    grpc_core::SetBit(&accepted_encodings, GRPC_COMPRESS_GZIP);
    grpc_core::SetBit(&accepted_encodings, GRPC_COMPRESS_DEFLATE);

    ASSERT_EQ(GRPC_COMPRESS_NONE,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_NONE,
                                                   accepted_encodings));

    ASSERT_EQ(GRPC_COMPRESS_GZIP,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_LOW,
                                                   accepted_encodings));

    ASSERT_EQ(GRPC_COMPRESS_DEFLATE,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_MED,
                                                   accepted_encodings));

    ASSERT_EQ(GRPC_COMPRESS_DEFLATE,
              grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_HIGH,
                                                   accepted_encodings));
  }
}

TEST(CompressionTest, CompressionEnableDisableAlgorithm) {
  grpc_compression_options options;
  grpc_compression_algorithm algorithm;

  gpr_log(GPR_DEBUG, "test_compression_enable_disable_algorithm");

  grpc_compression_options_init(&options);
  for (algorithm = GRPC_COMPRESS_NONE;
       algorithm < GRPC_COMPRESS_ALGORITHMS_COUNT;
       algorithm = static_cast<grpc_compression_algorithm>(
           static_cast<int>(algorithm) + 1)) {
    // all algorithms are enabled by default
    ASSERT_NE(
        grpc_compression_options_is_algorithm_enabled(&options, algorithm), 0);
  }
  // disable one by one
  for (algorithm = GRPC_COMPRESS_NONE;
       algorithm < GRPC_COMPRESS_ALGORITHMS_COUNT;
       algorithm = static_cast<grpc_compression_algorithm>(
           static_cast<int>(algorithm) + 1)) {
    grpc_compression_options_disable_algorithm(&options, algorithm);
    ASSERT_EQ(
        grpc_compression_options_is_algorithm_enabled(&options, algorithm), 0);
  }
  // re-enable one by one
  for (algorithm = GRPC_COMPRESS_NONE;
       algorithm < GRPC_COMPRESS_ALGORITHMS_COUNT;
       algorithm = static_cast<grpc_compression_algorithm>(
           static_cast<int>(algorithm) + 1)) {
    grpc_compression_options_enable_algorithm(&options, algorithm);
    ASSERT_NE(
        grpc_compression_options_is_algorithm_enabled(&options, algorithm), 0);
  }
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
