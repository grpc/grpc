//
//
// Copyright 2018 gRPC authors.
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

#include "src/core/tsi/alts/handshaker/transport_security_common_api.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <gtest/gtest.h>

const size_t kMaxRpcVersionMajor = 3;
const size_t kMaxRpcVersionMinor = 2;
const size_t kMinRpcVersionMajor = 2;
const size_t kMinRpcVersionMinor = 1;

static bool grpc_gcp_rpc_protocol_versions_equal(
    grpc_gcp_rpc_protocol_versions* l_versions,
    grpc_gcp_rpc_protocol_versions* r_versions) {
  EXPECT_TRUE(l_versions != nullptr && r_versions != nullptr);
  if ((l_versions->max_rpc_version.major !=
       r_versions->max_rpc_version.major) ||
      (l_versions->max_rpc_version.minor !=
       r_versions->max_rpc_version.minor)) {
    return false;
  }
  if ((l_versions->min_rpc_version.major !=
       r_versions->min_rpc_version.major) ||
      (l_versions->min_rpc_version.minor !=
       r_versions->min_rpc_version.minor)) {
    return false;
  }
  return true;
}

TEST(TransportSecurityCommonApiTest, TestSuccess) {
  grpc_gcp_rpc_protocol_versions version;
  grpc_gcp_rpc_protocol_versions decoded_version;
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_max(
      &version, kMaxRpcVersionMajor, kMaxRpcVersionMinor));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_min(
      &version, kMinRpcVersionMajor, kMinRpcVersionMinor));
  // Serializes to grpc slice.
  grpc_slice encoded_slice;
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_encode(&version, &encoded_slice));
  // Deserializes and compares with the original version.
  ASSERT_TRUE(
      grpc_gcp_rpc_protocol_versions_decode(encoded_slice, &decoded_version));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_equal(&version, &decoded_version));
  grpc_slice_unref(encoded_slice);
}

TEST(TransportSecurityCommonApiTest, TestFailure) {
  grpc_gcp_rpc_protocol_versions version, decoded_version;
  grpc_slice encoded_slice;
  // Test for invalid arguments.
  ASSERT_FALSE(grpc_gcp_rpc_protocol_versions_set_max(
      nullptr, kMaxRpcVersionMajor, kMaxRpcVersionMinor));
  ASSERT_FALSE(grpc_gcp_rpc_protocol_versions_set_min(
      nullptr, kMinRpcVersionMajor, kMinRpcVersionMinor));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_max(
      &version, kMaxRpcVersionMajor, kMaxRpcVersionMinor));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_min(
      &version, kMinRpcVersionMajor, kMinRpcVersionMinor));
  ASSERT_FALSE(grpc_gcp_rpc_protocol_versions_encode(nullptr, &encoded_slice));
  ASSERT_FALSE(grpc_gcp_rpc_protocol_versions_encode(&version, nullptr));
  ASSERT_FALSE(grpc_gcp_rpc_protocol_versions_decode(encoded_slice, nullptr));
  // Test for upb decode.
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_encode(&version, &encoded_slice));
  grpc_slice bad_slice = grpc_slice_split_head(
      &encoded_slice, GRPC_SLICE_LENGTH(encoded_slice) - 1);
  grpc_slice_unref(encoded_slice);
  ASSERT_TRUE(
      !grpc_gcp_rpc_protocol_versions_decode(bad_slice, &decoded_version));
  grpc_slice_unref(bad_slice);
}

TEST(TransportSecurityCommonApiTest, TestCopy) {
  grpc_gcp_rpc_protocol_versions src;
  grpc_gcp_rpc_protocol_versions des;
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_max(&src, kMaxRpcVersionMajor,
                                                     kMaxRpcVersionMinor));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_min(&src, kMinRpcVersionMajor,
                                                     kMinRpcVersionMinor));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_copy(&src, &des));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_equal(&src, &des));
}

TEST(TransportSecurityCommonApiTest, TestCheckSuccess) {
  grpc_gcp_rpc_protocol_versions v1;
  grpc_gcp_rpc_protocol_versions v2;
  grpc_gcp_rpc_protocol_versions_version highest_common_version;
  // test equality.
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_max(&v1, kMaxRpcVersionMajor,
                                                     kMaxRpcVersionMinor));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_min(&v1, kMaxRpcVersionMajor,
                                                     kMaxRpcVersionMinor));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_max(&v2, kMaxRpcVersionMajor,
                                                     kMaxRpcVersionMinor));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_min(&v2, kMaxRpcVersionMajor,
                                                     kMaxRpcVersionMinor));
  ASSERT_EQ(
      grpc_gcp_rpc_protocol_versions_check(
          (const grpc_gcp_rpc_protocol_versions*)&v1,
          (const grpc_gcp_rpc_protocol_versions*)&v2, &highest_common_version),
      1);
  ASSERT_EQ(grpc_core::internal::grpc_gcp_rpc_protocol_version_compare(
                &highest_common_version, &v1.max_rpc_version),
            0);

  // test inequality.
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_max(&v1, kMaxRpcVersionMajor,
                                                     kMaxRpcVersionMinor));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_min(&v1, kMinRpcVersionMinor,
                                                     kMinRpcVersionMinor));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_max(&v2, kMaxRpcVersionMajor,
                                                     kMinRpcVersionMinor));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_min(&v2, kMinRpcVersionMajor,
                                                     kMaxRpcVersionMinor));
  ASSERT_EQ(
      grpc_gcp_rpc_protocol_versions_check(
          (const grpc_gcp_rpc_protocol_versions*)&v1,
          (const grpc_gcp_rpc_protocol_versions*)&v2, &highest_common_version),
      1);
  ASSERT_EQ(grpc_core::internal::grpc_gcp_rpc_protocol_version_compare(
                &highest_common_version, &v2.max_rpc_version),
            0);
}

TEST(TransportSecurityCommonApiTest, TestCheckFailure) {
  grpc_gcp_rpc_protocol_versions v1;
  grpc_gcp_rpc_protocol_versions v2;
  grpc_gcp_rpc_protocol_versions_version highest_common_version;

  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_max(&v1, kMinRpcVersionMajor,
                                                     kMinRpcVersionMinor));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_min(&v1, kMinRpcVersionMajor,
                                                     kMinRpcVersionMinor));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_max(&v2, kMaxRpcVersionMajor,
                                                     kMaxRpcVersionMinor));
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_set_min(&v2, kMaxRpcVersionMajor,
                                                     kMaxRpcVersionMinor));
  ASSERT_EQ(
      grpc_gcp_rpc_protocol_versions_check(
          (const grpc_gcp_rpc_protocol_versions*)&v1,
          (const grpc_gcp_rpc_protocol_versions*)&v2, &highest_common_version),
      0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
