// Copyright 2023 gRPC authors.
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

#include "src/core/lib/gprpp/dns_domain.h"

#include "absl/strings/ascii.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace {

TEST(IsValidDnsDomain, Valid) {
  EXPECT_TRUE(IsValidDnsDomain("foo.bar.com"));
  EXPECT_TRUE(IsValidDnsDomain("FOO.BAR.COM"));
  EXPECT_TRUE(IsValidDnsDomain("f1.b2.c3"));
  EXPECT_TRUE(IsValidDnsDomain("F1.B2.C3"));
  EXPECT_TRUE(IsValidDnsDomain("abcdefghijklmnopqrstuvwxyz0123456789.com"));
  EXPECT_TRUE(IsValidDnsDomain("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.COM"));
}

TEST(IsValidDnsDomain, InValid) {
  EXPECT_FALSE(IsValidDnsDomain("1.com"));
  EXPECT_FALSE(IsValidDnsDomain("a..b"));
  EXPECT_FALSE(IsValidDnsDomain(".a"));
  for (unsigned char c = 0; ; ++c) {
    if (!absl::ascii_isalnum(c) && c != '.') {
      EXPECT_FALSE(IsValidDnsDomain(std::string(1, c)));
    }
    if (c == 255) break;
  }
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
