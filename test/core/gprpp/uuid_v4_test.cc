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

#include "src/core/lib/gprpp/uuid_v4.h"

#include "gtest/gtest.h"

namespace grpc_core {

namespace {

TEST(UUIDv4Test, Basic) {
  EXPECT_EQ(GenerateUUIDv4(0, 0), "00000000-0000-4000-8000-000000000000");
  EXPECT_EQ(GenerateUUIDv4(0x0123456789abcdef, 0x0123456789abcdef),
            "01234567-89ab-4def-8123-456789abcdef");
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
