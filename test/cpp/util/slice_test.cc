/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
