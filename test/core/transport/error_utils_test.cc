//
// Copyright 2016 gRPC authors.
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

#include "absl/status/status.h"
#include <gtest/gtest.h>

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
  char* cstr = grpc_slice_to_c_string(message);
  ASSERT_EQ(std::string(cstr), "");
  // Special error equivalent check
  ASSERT_EQ(error, GRPC_ERROR_NONE);

  gpr_free(cstr);
  grpc_slice_unref(message);
  GRPC_ERROR_UNREF(error);
}

TEST(ErrorUtilsTest, GrpcErrorNoneToAbslStatus) {
  absl::Status status = grpc_error_to_absl_status(GRPC_ERROR_NONE);
  ASSERT_TRUE(status.ok());
  ASSERT_EQ(status.message(), "");
}

// ---- Cancelled Status ----
TEST(ErrorUtilsTest, AbslCancelledToGrpcError) {
  grpc_error* error = absl_status_to_grpc_error(absl::CancelledError());
  // Status code checks
  intptr_t code;
  ASSERT_TRUE(grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &code));
  ASSERT_EQ(static_cast<grpc_status_code>(code), GRPC_STATUS_CANCELLED);
  // Status message checks
  grpc_slice message;
  ASSERT_TRUE(grpc_error_get_str(error, GRPC_ERROR_STR_GRPC_MESSAGE, &message));
  char* cstr = grpc_slice_to_c_string(message);
  ASSERT_EQ(std::string(cstr), "Cancelled");
  // Special error equivalent check
  ASSERT_EQ(error, GRPC_ERROR_CANCELLED);

  gpr_free(cstr);
  grpc_slice_unref(message);
  GRPC_ERROR_UNREF(error);
}

TEST(ErrorUtilsTest, GrpcErrorCancelledToAbslStatus) {
  absl::Status status = grpc_error_to_absl_status(GRPC_ERROR_CANCELLED);
  ASSERT_TRUE(absl::IsCancelled(status));
  ASSERT_EQ(status.message(), "Cancelled");
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
  ASSERT_TRUE(grpc_error_get_str(error, GRPC_ERROR_STR_GRPC_MESSAGE, &message));
  char* cstr = grpc_slice_to_c_string(message);
  ASSERT_EQ(std::string(cstr), "Out of memory");
  // Special error equivalent check
  ASSERT_EQ(error, GRPC_ERROR_OOM);

  gpr_free(cstr);
  grpc_slice_unref(message);
  GRPC_ERROR_UNREF(error);
}

TEST(ErrorUtilsTest, AbslNonOOMResourceExhaustedToGrpcErrorIsNotSpecial) {
  grpc_error* error =
      absl_status_to_grpc_error(absl::ResourceExhaustedError("Lemonade"));
  // Status code checks
  intptr_t code;
  ASSERT_TRUE(grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &code));
  ASSERT_EQ(static_cast<grpc_status_code>(code),
            GRPC_STATUS_RESOURCE_EXHAUSTED);
  // Status message checks
  grpc_slice message;
  // Oddly, the GRPC_MESSAGE is not set by GRPC_ERROR_CREATE_FROM_COPIED_STRING
  ASSERT_FALSE(
      grpc_error_get_str(error, GRPC_ERROR_STR_GRPC_MESSAGE, &message));
  // ... However, the DESCREPTION is
  ASSERT_TRUE(grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &message));
  char* cstr = grpc_slice_to_c_string(message);
  ASSERT_EQ(std::string(cstr), "Lemonade");
  // Special error equivalent check.
  ASSERT_NE(error, GRPC_ERROR_OOM);

  gpr_free(cstr);
  grpc_slice_unref(message);
  GRPC_ERROR_UNREF(error);
}

TEST(ErrorUtilsTest, GrpcErrorOOMToAbslStatus) {
  absl::Status status = grpc_error_to_absl_status(GRPC_ERROR_OOM);
  ASSERT_TRUE(absl::IsResourceExhausted(status));
  ASSERT_EQ(status.message(), "Out of memory");
}

}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  return RUN_ALL_TESTS();
};
