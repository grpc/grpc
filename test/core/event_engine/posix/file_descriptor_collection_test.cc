// Copyright 2025 The gRPC Authors
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

#include "src/core/lib/event_engine/posix_engine/file_descriptor_collection.h"

#include <grpc/grpc.h>

#include "src/core/lib/experiments/experiments.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_event_engine::experimental {

bool ForkEnabled() {
#ifndef GRPC_ENABLE_FORK_SUPPORT
  return false;
#else
  return grpc_core::IsEventEngineForkEnabled();
#endif
}

TEST(FileDescriptorCollection, AddRecordsGenerationClearClears) {
  FileDescriptorCollection collection(42);
  EXPECT_EQ(collection.Add(10), FileDescriptor(10, 42));
  EXPECT_EQ(collection.Add(12), FileDescriptor(12, 42));
  if (ForkEnabled()) {
    EXPECT_THAT(collection.ClearAndReturnRawDescriptors(),
                ::testing::UnorderedElementsAre(10, 12));
  } else {
    EXPECT_THAT(collection.ClearAndReturnRawDescriptors(),
                ::testing::IsEmpty());
  }
}

TEST(FileDescriptorCollectionTest, RemoveHonorsGeneration) {
  FileDescriptorCollection collection(2);
  collection.Add(7);
  // Untracked
  EXPECT_EQ(collection.Remove(FileDescriptor(6, 2)), !ForkEnabled());
  // Wrong generation
  EXPECT_EQ(collection.Remove(FileDescriptor(7, 1)), !ForkEnabled());
  // Correct
  EXPECT_TRUE(collection.Remove(FileDescriptor(7, 2)));
  // Already gone
  EXPECT_EQ(collection.Remove(FileDescriptor(7, 2)), !ForkEnabled());
}

// Regression test for https://github.com/grpc/grpc/issues/40371
// fd 0 is a valid file descriptor (e.g. when stdin is closed before forking)
// and must not be treated as invalid.
TEST(FileDescriptorTest, ZeroFdIsValid) {
  FileDescriptor fd_zero(0, 1);
  EXPECT_TRUE(fd_zero.ready());
  EXPECT_EQ(fd_zero.fd(), 0);
}

TEST(FileDescriptorTest, NegativeFdIsInvalid) {
  FileDescriptor fd_neg(-1, 1);
  EXPECT_FALSE(fd_neg.ready());
  EXPECT_EQ(fd_neg.fd(), -1);
}

TEST(FileDescriptorTest, DefaultConstructedIsInvalid) {
  FileDescriptor fd_default;
  EXPECT_FALSE(fd_default.ready());
}

TEST(FileDescriptorCollectionTest, AddZeroFd) {
  FileDescriptorCollection collection(1);
  FileDescriptor fd = collection.Add(0);
  EXPECT_TRUE(fd.ready());
  EXPECT_EQ(fd.fd(), 0);
  EXPECT_EQ(fd.generation(), ForkEnabled() ? 1 : 0);
  if (ForkEnabled()) {
    EXPECT_THAT(collection.ClearAndReturnRawDescriptors(),
                ::testing::UnorderedElementsAre(0));
  } else {
    EXPECT_THAT(collection.ClearAndReturnRawDescriptors(),
                ::testing::IsEmpty());
  }
}

}  // namespace grpc_event_engine::experimental

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}