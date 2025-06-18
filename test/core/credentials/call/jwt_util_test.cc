//
// Copyright 2025 gRPC authors.
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

#include "src/core/credentials/call/jwt_util.h"

#include <grpc/support/time.h>

#include <string>

#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace {

TEST(GetJwtExpirationTime, Valid) {
  std::string token = absl::StrCat(
      "foo.", absl::WebSafeBase64Escape("{\"exp\":499996800}"), ".bar");
  auto result = GetJwtExpirationTime(token);
  ASSERT_TRUE(result.ok()) << result.status();
  gpr_timespec expiration = result->as_timespec(GPR_CLOCK_REALTIME);
  EXPECT_EQ(expiration.tv_sec, 499996800);
}

TEST(GetJwtExpirationTime, TokenHasWrongNumberOfDots) {
  EXPECT_EQ(GetJwtExpirationTime("foo.bar").status(),
            absl::UnauthenticatedError("error parsing JWT token"));
}

TEST(GetJwtExpirationTime, TokenPayloadNotBase64) {
  EXPECT_EQ(GetJwtExpirationTime("foo.&.bar").status(),
            absl::UnauthenticatedError("error parsing JWT token"));
}

TEST(GetJwtExpirationTime, TokenPayloadNotJson) {
  std::string token =
      absl::StrCat("foo.", absl::WebSafeBase64Escape("xxx"), ".bar");
  EXPECT_EQ(GetJwtExpirationTime(token).status(),
            absl::UnauthenticatedError("error parsing JWT token"));
}

TEST(GetJwtExpirationTime, TokenInvalidExpiration) {
  std::string token = absl::StrCat(
      "foo.", absl::WebSafeBase64Escape("{\"exp\":\"foo\"}"), ".bar");
  EXPECT_EQ(GetJwtExpirationTime(token).status(),
            absl::UnauthenticatedError("error parsing JWT token"));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
