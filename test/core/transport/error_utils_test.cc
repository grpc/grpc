//
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
//

#include "src/core/lib/transport/error_utils.h"

#include <stdint.h>

#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/status_helper.h"
#include "test/core/test_util/test_config.h"

namespace {

TEST(ErrorUtilsTest, GetErrorGetStatusNone) {
  grpc_error_handle error;
  grpc_status_code code;
  std::string message;
  grpc_error_get_status(error, grpc_core::Timestamp(), &code, &message, nullptr,
                        nullptr);
  ASSERT_EQ(code, GRPC_STATUS_OK);
  ASSERT_EQ(message, "");
}

TEST(ErrorUtilsTest, GetErrorGetStatusFlat) {
  grpc_error_handle error = grpc_error_set_int(
      GRPC_ERROR_CREATE("Msg"), grpc_core::StatusIntProperty::kRpcStatus,
      GRPC_STATUS_CANCELLED);
  grpc_status_code code;
  std::string message;
  grpc_error_get_status(error, grpc_core::Timestamp(), &code, &message, nullptr,
                        nullptr);
  ASSERT_EQ(code, GRPC_STATUS_CANCELLED);
  ASSERT_EQ(message, "Msg");
}

TEST(ErrorUtilsTest, GetErrorGetStatusChild) {
  std::vector<grpc_error_handle> children = {
      GRPC_ERROR_CREATE("Child1"),
      grpc_error_set_int(GRPC_ERROR_CREATE("Child2"),
                         grpc_core::StatusIntProperty::kRpcStatus,
                         GRPC_STATUS_RESOURCE_EXHAUSTED),
  };
  grpc_error_handle error = GRPC_ERROR_CREATE_FROM_VECTOR("Parent", &children);
  grpc_status_code code;
  std::string message;
  grpc_error_get_status(error, grpc_core::Timestamp(), &code, &message, nullptr,
                        nullptr);
  ASSERT_EQ(code, GRPC_STATUS_RESOURCE_EXHAUSTED);
  ASSERT_EQ(message, "Child2");
}

// ---- Ok Status ----
TEST(ErrorUtilsTest, AbslOkToGrpcError) {
  grpc_error_handle error = absl_status_to_grpc_error(absl::OkStatus());
  ASSERT_EQ(absl::OkStatus(), error);
}

TEST(ErrorUtilsTest, GrpcSpecialErrorNoneToAbslStatus) {
  absl::Status status = grpc_error_to_absl_status(absl::OkStatus());
  ASSERT_TRUE(status.ok());
  ASSERT_EQ(status.message(), "");
}

// ---- Asymmetry of conversions of "Special" errors ----
TEST(ErrorUtilsTest, AbslStatusToGrpcErrorDoesNotReturnSpecialVariables) {
  grpc_error_handle error =
      absl_status_to_grpc_error(absl::CancelledError("CANCELLED"));
  ASSERT_NE(error, absl::CancelledError());
}

TEST(ErrorUtilsTest, GrpcSpecialErrorCancelledToAbslStatus) {
  absl::Status status = grpc_error_to_absl_status(absl::CancelledError());
  ASSERT_TRUE(absl::IsCancelled(status));
  ASSERT_EQ(status.message(), "CANCELLED");
}

TEST(ErrorUtilsTest, GrpcSpecialErrorOOMToAbslStatus) {
  absl::Status status =
      grpc_error_to_absl_status(absl::ResourceExhaustedError(""));
  ASSERT_TRUE(absl::IsResourceExhausted(status));
  ASSERT_EQ(status.message(), "RESOURCE_EXHAUSTED");
}

// ---- Ordinary statuses ----
TEST(ErrorUtilsTest, AbslUnavailableToGrpcError) {
  grpc_error_handle error =
      absl_status_to_grpc_error(absl::UnavailableError("Making tea"));
  // Status code checks
  intptr_t code;
  ASSERT_TRUE(grpc_error_get_int(
      error, grpc_core::StatusIntProperty::kRpcStatus, &code));
  ASSERT_EQ(static_cast<grpc_status_code>(code), GRPC_STATUS_UNAVAILABLE);
  // Status message checks
  ASSERT_EQ(error.message(), "Making tea");
}

TEST(ErrorUtilsTest, GrpcErrorUnavailableToAbslStatus) {
  grpc_error_handle error = grpc_error_set_int(
      GRPC_ERROR_CREATE(
          "weighted_target: all children report state TRANSIENT_FAILURE"),
      grpc_core::StatusIntProperty::kRpcStatus, GRPC_STATUS_UNAVAILABLE);
  absl::Status status = grpc_error_to_absl_status(error);
  ASSERT_TRUE(absl::IsUnavailable(status));
  ASSERT_EQ(status.message(),
            "weighted_target: all children report state TRANSIENT_FAILURE");
}

}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
};
