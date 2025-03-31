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

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/experiments/experiments.h"

namespace grpc_event_engine::experimental {

namespace {
constexpr int kIntFdBits = FileDescriptorCollection::kIntFdBits;

bool ForkEnabled() {
#ifndef GRPC_ENABLE_FORK_SUPPORT
  return false;
#else
  return grpc_core::IsEventEngineForkEnabled();
#endif
}

int ExpectedFdWithGeneration(int fd, int gen) {
  if (ForkEnabled()) {
    return fd +
           ((gen & FileDescriptorCollection::kGenerationMask) << kIntFdBits);
  } else {
    return fd;
  }
}
}  // namespace

TEST(FileDescriptorCollectionTest, AdvanceGeneration) {
  if (!ForkEnabled()) {
    GTEST_SKIP() << "Fork is not enabled";
  }
  FileDescriptorCollection collection;
  EXPECT_EQ(collection.generation(), 1);
  EXPECT_EQ(collection.Add(5).generation(), 1);
  EXPECT_EQ(collection.Add(8).generation(), 1);
  EXPECT_THAT(collection.AdvanceGeneration(),
              ::testing::UnorderedElementsAre(5, 8));
  // 8 will still be removed because it is a different 8
  EXPECT_EQ(collection.Add(8).generation(), 2);
  EXPECT_THAT(collection.AdvanceGeneration(),
              ::testing::UnorderedElementsAre(8));
  // Test remove affects the list of fds
  EXPECT_EQ(collection.Add(5).generation(), 3);
  EXPECT_EQ(collection.Add(8).generation(), 3);
  collection.Remove(FileDescriptor(8, 3));
  EXPECT_EQ(collection.Add(10).generation(), 3);
  // Wrong generation should not be removed
  collection.Remove(FileDescriptor(10, 1));
  EXPECT_THAT(collection.AdvanceGeneration(),
              ::testing::UnorderedElementsAre(5, 10));
}

TEST(FileDescriptorCollectionTest, ToInteger) {
  FileDescriptorCollection collection;
  auto fd1 = collection.Add(5);
  EXPECT_EQ(collection.ToInteger(fd1), ExpectedFdWithGeneration(5, 1));
  collection.AdvanceGeneration();
  collection.AdvanceGeneration();
  collection.AdvanceGeneration();
  // Still uses the FD generation
  EXPECT_EQ(collection.ToInteger(fd1), ExpectedFdWithGeneration(5, 1));
  auto fd2 = collection.Add(5);
  EXPECT_EQ(collection.ToInteger(fd2), ExpectedFdWithGeneration(5, 4));
  for (size_t i = 0; i < 30; ++i) {
    collection.AdvanceGeneration();
  }
  EXPECT_EQ(collection.ToInteger(collection.Add(3)),
            ExpectedFdWithGeneration(3, 34));
}

TEST(FileDescriptorCollectionTest, FromInteger) {
  FileDescriptorCollection collection;
  // More generation that mask would track
  for (size_t i = 0; i < 30; ++i) {
    collection.AdvanceGeneration();
  }
  int gen =
      (collection.generation() & FileDescriptorCollection::kGenerationMask)
      << kIntFdBits;
  EXPECT_EQ(collection.FromInteger(0)->iomgr_fd(), -1);
  EXPECT_EQ(collection.FromInteger(gen + 7)->iomgr_fd(),
            ForkEnabled() ? 7 : gen + 7);
  EXPECT_EQ(collection.FromInteger(gen + 7)->generation(),
            ForkEnabled() ? 31 : 0);
  auto result = collection.FromInteger((2 << kIntFdBits) + 7);
  if (ForkEnabled()) {
    EXPECT_TRUE(result.IsWrongGenerationError());
  } else {
    EXPECT_TRUE(result.ok());
  }
}

TEST(FileDescriptorCollectionTest, Remove) {
  FileDescriptorCollection collection;
  collection.AdvanceGeneration();
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

}  // namespace grpc_event_engine::experimental

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // TODO(ctiller): EventEngine temporarily needs grpc to be initialized first
  // until we clear out the iomgr shutdown code.
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}