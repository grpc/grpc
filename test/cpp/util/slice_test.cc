/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc++/support/slice.h>

#include <grpc/slice.h>
#include <gtest/gtest.h>

namespace grpc {
namespace {

const char* kContent = "hello xxxxxxxxxxxxxxxxxxxx world";

class SliceTest : public ::testing::Test {
 protected:
  void CheckSlice(const Slice& s, const grpc::string& content) {
    EXPECT_EQ(content.size(), s.size());
    EXPECT_EQ(content,
              grpc::string(reinterpret_cast<const char*>(s.begin()), s.size()));
  }
};

TEST_F(SliceTest, Steal) {
  grpc_slice s = grpc_slice_from_copied_string(kContent);
  Slice spp(s, Slice::STEAL_REF);
  CheckSlice(spp, kContent);
}

TEST_F(SliceTest, Add) {
  grpc_slice s = grpc_slice_from_copied_string(kContent);
  Slice spp(s, Slice::ADD_REF);
  grpc_slice_unref(s);
  CheckSlice(spp, kContent);
}

TEST_F(SliceTest, Empty) {
  Slice empty_slice;
  CheckSlice(empty_slice, "");
}

TEST_F(SliceTest, Cslice) {
  grpc_slice s = grpc_slice_from_copied_string(kContent);
  Slice spp(s, Slice::STEAL_REF);
  CheckSlice(spp, kContent);
  grpc_slice c_slice = spp.c_slice();
  EXPECT_EQ(GRPC_SLICE_START_PTR(s), GRPC_SLICE_START_PTR(c_slice));
  EXPECT_EQ(GRPC_SLICE_END_PTR(s), GRPC_SLICE_END_PTR(c_slice));
  grpc_slice_unref(c_slice);
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
