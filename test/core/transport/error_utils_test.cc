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

#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/status_helper.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

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
  grpc_error_handle error = absl::CancelledError("Msg");
  grpc_status_code code;
  std::string message;
  grpc_error_get_status(error, grpc_core::Timestamp(), &code, &message, nullptr,
                        nullptr);
  ASSERT_EQ(code, GRPC_STATUS_CANCELLED);
  ASSERT_EQ(message, "Msg");
}

TEST(ErrorUtilsTest, GetErrorGetStatusChild) {
  std::vector<grpc_error_handle> children = {
      absl::UnknownError("Child1"),
      absl::ResourceExhaustedError("Child2"),
  };
  grpc_error_handle error = GRPC_ERROR_CREATE_FROM_VECTOR("Parent", &children);
  grpc_status_code code;
  std::string message;
  grpc_error_get_status(error, grpc_core::Timestamp(), &code, &message, nullptr,
                        nullptr);
  ASSERT_EQ(code, GRPC_STATUS_RESOURCE_EXHAUSTED);
  ASSERT_EQ(message, "Parent (Child1) (Child2)");
}

}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
};
