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

#include <grpc++/support/byte_buffer.h>

#include <cstring>
#include <vector>

#include <grpc/support/slice.h>
#include <grpc++/support/slice.h>
#include <gtest/gtest.h>

namespace grpc {
namespace {

const char* kContent1 = "hello xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
const char* kContent2 = "yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy world";

class ByteBufferTest : public ::testing::Test {};

TEST_F(ByteBufferTest, CreateFromSingleSlice) {
  gpr_slice hello = gpr_slice_from_copied_string(kContent1);
  Slice s(hello, Slice::STEAL_REF);
  ByteBuffer buffer(&s, 1);
}

TEST_F(ByteBufferTest, CreateFromVector) {
  gpr_slice hello = gpr_slice_from_copied_string(kContent1);
  gpr_slice world = gpr_slice_from_copied_string(kContent2);
  std::vector<Slice> slices;
  slices.push_back(Slice(hello, Slice::STEAL_REF));
  slices.push_back(Slice(world, Slice::STEAL_REF));
  ByteBuffer buffer(&slices[0], 2);
}

TEST_F(ByteBufferTest, Clear) {
  gpr_slice hello = gpr_slice_from_copied_string(kContent1);
  Slice s(hello, Slice::STEAL_REF);
  ByteBuffer buffer(&s, 1);
  buffer.Clear();
}

TEST_F(ByteBufferTest, Length) {
  gpr_slice hello = gpr_slice_from_copied_string(kContent1);
  gpr_slice world = gpr_slice_from_copied_string(kContent2);
  std::vector<Slice> slices;
  slices.push_back(Slice(hello, Slice::STEAL_REF));
  slices.push_back(Slice(world, Slice::STEAL_REF));
  ByteBuffer buffer(&slices[0], 2);
  EXPECT_EQ(strlen(kContent1) + strlen(kContent2), buffer.Length());
}

bool SliceEqual(const Slice& a, gpr_slice b) {
  if (a.size() != GPR_SLICE_LENGTH(b)) {
    return false;
  }
  for (size_t i = 0; i < a.size(); i++) {
    if (a.begin()[i] != GPR_SLICE_START_PTR(b)[i]) {
      return false;
    }
  }
  return true;
}

TEST_F(ByteBufferTest, Dump) {
  gpr_slice hello = gpr_slice_from_copied_string(kContent1);
  gpr_slice world = gpr_slice_from_copied_string(kContent2);
  std::vector<Slice> slices;
  slices.push_back(Slice(hello, Slice::STEAL_REF));
  slices.push_back(Slice(world, Slice::STEAL_REF));
  ByteBuffer buffer(&slices[0], 2);
  slices.clear();
  buffer.Dump(&slices);
  EXPECT_TRUE(SliceEqual(slices[0], hello));
  EXPECT_TRUE(SliceEqual(slices[1], world));
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
