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

#include <gtest/gtest.h>
#include "absl/status/status.h"

#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

namespace {

// ---- Ok Status ----
TEST(ErrorUtilsTest, AbslOkToGrpcError) {
  grpc_error* error = absl_status_to_grpc_error(absl::OkStatus());
  // Status code checks
  intptr_t code;
  ASSERT_TRUE(grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &code));
  ASSERT_EQ(static_cast<grpc_status_code>(code), GRPC_STATUS_OK);
  // Status message checks
  grpc_slice message;
  ASSERT_TRUE(grpc_error_get_str(error, GRPC_ERROR_STR_GRPC_MESSAGE, &message));
  absl::string_view str = grpc_core::StringViewFromSlice(message);
  ASSERT_EQ(str, "");
  // Special error equivalent check
  ASSERT_EQ(error, GRPC_ERROR_NONE);
  grpc_slice_unref(message);
  GRPC_ERROR_UNREF(error);
}

TEST(ErrorUtilsTest, GrpcSpecialErrorNoneToAbslStatus) {
  absl::Status status = grpc_error_to_absl_status(GRPC_ERROR_NONE);
  ASSERT_TRUE(status.ok());
  ASSERT_EQ(status.message(), "");
}

// Loosely equivalent to GRPC_ERROR_NONE
TEST(ErrorUtilsTest, GrpcOrdinaryErrorOkToAbslStatus) {
  grpc_error* error =
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(""),
                         GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_OK);
  absl::Status status = grpc_error_to_absl_status(error);
  ASSERT_TRUE(status.ok());
  ASSERT_EQ(status.message(), "");
}

// ---- Cancelled Status ----
TEST(ErrorUtilsTest, AbslCancelledToGrpcError) {
  grpc_error* error =
      absl_status_to_grpc_error(absl::CancelledError("Cancelled"));
  // Status code checks
  intptr_t code;
  ASSERT_TRUE(grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &code));
  ASSERT_EQ(static_cast<grpc_status_code>(code), GRPC_STATUS_CANCELLED);
  // Status message checks
  grpc_slice message;
  ASSERT_TRUE(grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &message));
  absl::string_view str = grpc_core::StringViewFromSlice(message);
  ASSERT_EQ(str, "Cancelled");
  // Note that this conversion will never return the special case variables.
  ASSERT_NE(error, GRPC_ERROR_CANCELLED);
  grpc_slice_unref(message);
  GRPC_ERROR_UNREF(error);
}

TEST(ErrorUtilsTest, GrpcSpecialErrorCancelledToAbslStatus) {
  absl::Status status = grpc_error_to_absl_status(GRPC_ERROR_CANCELLED);
  ASSERT_TRUE(absl::IsCancelled(status));
  ASSERT_EQ(status.message(), "Cancelled");
}

TEST(ErrorUtilsTest, GrpcOrdinaryErrorCancelledToAbslStatus) {
  grpc_error* error = grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("User changed their mind"),
      GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_CANCELLED);
  absl::Status status = grpc_error_to_absl_status(error);
  ASSERT_TRUE(absl::IsCancelled(status));
  ASSERT_EQ(status.message(), "User changed their mind");
}

// ---- OOM Status ----
TEST(ErrorUtilsTest, AbslOOMToGrpcError) {
  grpc_error* error =
      absl_status_to_grpc_error(absl::ResourceExhaustedError("Out of memory"));
  // Status code checks
  intptr_t code;
  ASSERT_TRUE(grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &code));
  ASSERT_EQ(static_cast<grpc_status_code>(code),
            GRPC_STATUS_RESOURCE_EXHAUSTED);
  // Status message checks
  grpc_slice message;
  ASSERT_TRUE(grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &message));
  absl::string_view str = grpc_core::StringViewFromSlice(message);
  ASSERT_EQ(str, "Out of memory");
  // Note that this conversion will never return the special case variables.
  ASSERT_NE(error, GRPC_ERROR_OOM);
  grpc_slice_unref(message);
  GRPC_ERROR_UNREF(error);
}

TEST(ErrorUtilsTest, AbslNonOOMResourceExhaustedToGrpcError) {
  grpc_error* error =
      absl_status_to_grpc_error(absl::ResourceExhaustedError("Lemonade"));
  // Status code checks
  intptr_t code;
  ASSERT_TRUE(grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &code));
  ASSERT_EQ(static_cast<grpc_status_code>(code),
            GRPC_STATUS_RESOURCE_EXHAUSTED);
  // Status message checks
  grpc_slice message;
  ASSERT_TRUE(grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &message));
  absl::string_view str = grpc_core::StringViewFromSlice(message);
  ASSERT_EQ(str, "Lemonade");
  // Special error equivalent check.
  ASSERT_NE(error, GRPC_ERROR_OOM);
  grpc_slice_unref(message);
  GRPC_ERROR_UNREF(error);
}

TEST(ErrorUtilsTest, GrpcSpecialErrorOOMToAbslStatus) {
  absl::Status status = grpc_error_to_absl_status(GRPC_ERROR_OOM);
  ASSERT_TRUE(absl::IsResourceExhausted(status));
  ASSERT_EQ(status.message(), "Out of memory");
}

TEST(ErrorUtilsTest, GrpcOrdinaryErrorOOMToAbslStatus) {
  grpc_error* error = grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Out of memory"),
      GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_RESOURCE_EXHAUSTED);
  absl::Status status = grpc_error_to_absl_status(error);
  ASSERT_TRUE(absl::IsResourceExhausted(status));
  ASSERT_EQ(status.message(), "Out of memory");
}

// ---- Other Non-special statuses ----
TEST(ErrorUtilsTest, AbslUnavailableToGrpcError) {
  grpc_error* error =
      absl_status_to_grpc_error(absl::UnavailableError("Making tea"));
  // Status code checks
  intptr_t code;
  ASSERT_TRUE(grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &code));
  ASSERT_EQ(static_cast<grpc_status_code>(code), GRPC_STATUS_UNAVAILABLE);
  // Status message checks
  grpc_slice message;
  ASSERT_TRUE(grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &message));
  absl::string_view str = grpc_core::StringViewFromSlice(message);
  ASSERT_EQ(str, "Making tea");
  grpc_slice_unref(message);
  GRPC_ERROR_UNREF(error);
}

TEST(ErrorUtilsTest, GrpcErrorUnavailableToAbslStatus) {
  grpc_error* error = grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "weighted_target: all children report state TRANSIENT_FAILURE"),
      GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
  absl::Status status = grpc_error_to_absl_status(error);
  ASSERT_TRUE(absl::IsUnavailable(status));
  ASSERT_EQ(status.message(),
            "weighted_target: all children report state TRANSIENT_FAILURE");
}

}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  return RUN_ALL_TESTS();
};
