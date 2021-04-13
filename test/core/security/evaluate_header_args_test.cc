// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/lib/security/authorization/evaluate_header_args.h"
#include "test/core/util/test_config.h"

namespace grpc_core {

TEST(EvaluateHeaderArgsTest, HandlesNullMetadata) {
  EvaluateHeaderArgs args;
  EXPECT_EQ(args.GetPath(), nullptr);
  EXPECT_EQ(args.GetMethod(), nullptr);
  EXPECT_EQ(args.GetHost(), nullptr);
  EXPECT_THAT(args.GetHeaders(), ::testing::ElementsAre());
  EXPECT_EQ(args.GetHeaderValue("some_key", nullptr), absl::nullopt);
}

TEST(EvaluateHeaderArgsTest, HandlesEmptyMetadata) {
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  EvaluateHeaderArgs args(&metadata);
  EXPECT_EQ(args.GetPath(), nullptr);
  EXPECT_EQ(args.GetMethod(), nullptr);
  EXPECT_EQ(args.GetHost(), nullptr);
  EXPECT_THAT(args.GetHeaders(), ::testing::ElementsAre());
  EXPECT_EQ(args.GetHeaderValue("some_key", nullptr), absl::nullopt);
  grpc_metadata_batch_destroy(&metadata);
}

TEST(EvaluateHeaderArgsTest, GetPathSuccess) {
  grpc_init();
  const char* kPath = "/some/path";
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_slice fake_val = grpc_slice_intern(grpc_slice_from_static_string(kPath));
  grpc_mdelem fake_val_md = grpc_mdelem_from_slices(GRPC_MDSTR_PATH, fake_val);
  grpc_linked_mdelem storage;
  storage.md = fake_val_md;
  ASSERT_EQ(grpc_metadata_batch_link_head(&metadata, &storage),
            GRPC_ERROR_NONE);
  EvaluateHeaderArgs args(&metadata);
  EXPECT_EQ(args.GetPath(), kPath);
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateHeaderArgsTest, GetHostSuccess) {
  grpc_init();
  const char* kHost = "host";
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_slice fake_val = grpc_slice_intern(grpc_slice_from_static_string(kHost));
  grpc_mdelem fake_val_md = grpc_mdelem_from_slices(GRPC_MDSTR_HOST, fake_val);
  grpc_linked_mdelem storage;
  storage.md = fake_val_md;
  ASSERT_EQ(grpc_metadata_batch_link_head(&metadata, &storage),
            GRPC_ERROR_NONE);
  EvaluateHeaderArgs args(&metadata);
  EXPECT_EQ(args.GetHost(), kHost);
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateHeaderArgsTest, GetMethodSuccess) {
  grpc_init();
  const char* kMethod = "GET";
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_slice fake_val =
      grpc_slice_intern(grpc_slice_from_static_string(kMethod));
  grpc_mdelem fake_val_md =
      grpc_mdelem_from_slices(GRPC_MDSTR_METHOD, fake_val);
  grpc_linked_mdelem storage;
  storage.md = fake_val_md;
  ASSERT_EQ(grpc_metadata_batch_link_head(&metadata, &storage),
            GRPC_ERROR_NONE);
  EvaluateHeaderArgs args(&metadata);
  EXPECT_EQ(args.GetMethod(), kMethod);
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateHeaderArgsTest, GetHeadersSuccess) {
  grpc_init();
  const char* kPath = "/some/path";
  const char* kHost = "host";
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_slice fake_path =
      grpc_slice_intern(grpc_slice_from_static_string(kPath));
  grpc_mdelem fake_path_md =
      grpc_mdelem_from_slices(GRPC_MDSTR_PATH, fake_path);
  grpc_linked_mdelem storage;
  storage.md = fake_path_md;
  ASSERT_EQ(grpc_metadata_batch_link_head(&metadata, &storage, GRPC_BATCH_PATH),
            GRPC_ERROR_NONE);
  grpc_slice fake_host =
      grpc_slice_intern(grpc_slice_from_static_string(kHost));
  grpc_mdelem fake_host_md =
      grpc_mdelem_from_slices(GRPC_MDSTR_HOST, fake_host);
  grpc_linked_mdelem storage2;
  storage2.md = fake_host_md;
  ASSERT_EQ(
      grpc_metadata_batch_link_tail(&metadata, &storage2, GRPC_BATCH_HOST),
      GRPC_ERROR_NONE);
  EvaluateHeaderArgs args(&metadata);
  EXPECT_THAT(
      args.GetHeaders(),
      ::testing::UnorderedElementsAre(
          ::testing::Pair(StringViewFromSlice(GRPC_MDSTR_HOST), kHost),
          ::testing::Pair(StringViewFromSlice(GRPC_MDSTR_PATH), kPath)));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateHeaderArgsTest, GetHeaderValueSuccess) {
  grpc_init();
  const char* kKey = "some_key";
  const char* kValue = "some_value";
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  storage.md = grpc_mdelem_from_slices(
      grpc_slice_intern(grpc_slice_from_static_string(kKey)),
      grpc_slice_intern(grpc_slice_from_static_string(kValue)));
  ASSERT_EQ(grpc_metadata_batch_link_head(&metadata, &storage),
            GRPC_ERROR_NONE);
  EvaluateHeaderArgs args(&metadata);
  std::string concatenated_value;
  absl::optional<absl::string_view> value =
      args.GetHeaderValue(kKey, &concatenated_value);
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), kValue);
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
