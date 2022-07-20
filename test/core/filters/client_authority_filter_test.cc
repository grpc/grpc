// Copyright 2022 gRPC authors.
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

#include "src/core/ext/filters/http/client_authority_filter.h"

#include <gtest/gtest.h>

#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/promise/test_context.h"

namespace grpc_core {
namespace {

auto* g_memory_allocator = new MemoryAllocator(
    ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));

ChannelArgs TestChannelArgs(absl::string_view default_authority) {
  return ChannelArgs().Set(GRPC_ARG_DEFAULT_AUTHORITY, default_authority);
}

TEST(ClientAuthorityFilterTest, DefaultFails) {
  EXPECT_FALSE(
      ClientAuthorityFilter::Create(ChannelArgs(), ChannelFilter::Args()).ok());
}

TEST(ClientAuthorityFilterTest, WithArgSucceeds) {
  EXPECT_EQ(ClientAuthorityFilter::Create(TestChannelArgs("foo.test.google.au"),
                                          ChannelFilter::Args())
                .status(),
            absl::OkStatus());
}

TEST(ClientAuthorityFilterTest, NonStringArgFails) {
  EXPECT_FALSE(ClientAuthorityFilter::Create(
                   ChannelArgs().Set(GRPC_ARG_DEFAULT_AUTHORITY, 123),
                   ChannelFilter::Args())
                   .ok());
}

TEST(ClientAuthorityFilterTest, PromiseCompletesImmediatelyAndSetsAuthority) {
  auto filter = *ClientAuthorityFilter::Create(
      TestChannelArgs("foo.test.google.au"), ChannelFilter::Args());
  auto arena = MakeScopedArena(1024, g_memory_allocator);
  grpc_metadata_batch initial_metadata_batch(arena.get());
  grpc_metadata_batch trailing_metadata_batch(arena.get());
  bool seen = false;
  // TODO(ctiller): use Activity here, once it's ready.
  TestContext<Arena> context(arena.get());
  auto promise = filter.MakeCallPromise(
      CallArgs{
          ClientMetadataHandle::TestOnlyWrap(&initial_metadata_batch),
          nullptr,
      },
      [&](CallArgs call_args) {
        EXPECT_EQ(call_args.client_initial_metadata
                      ->get_pointer(HttpAuthorityMetadata())
                      ->as_string_view(),
                  "foo.test.google.au");
        seen = true;
        return ArenaPromise<ServerMetadataHandle>(
            [&]() -> Poll<ServerMetadataHandle> {
              return ServerMetadataHandle::TestOnlyWrap(
                  &trailing_metadata_batch);
            });
      });
  auto result = promise();
  EXPECT_TRUE(absl::get_if<ServerMetadataHandle>(&result) != nullptr);
  EXPECT_TRUE(seen);
}

TEST(ClientAuthorityFilterTest,
     PromiseCompletesImmediatelyAndDoesNotClobberAlreadySetsAuthority) {
  auto filter = *ClientAuthorityFilter::Create(
      TestChannelArgs("foo.test.google.au"), ChannelFilter::Args());
  auto arena = MakeScopedArena(1024, g_memory_allocator);
  grpc_metadata_batch initial_metadata_batch(arena.get());
  grpc_metadata_batch trailing_metadata_batch(arena.get());
  initial_metadata_batch.Set(HttpAuthorityMetadata(),
                             Slice::FromStaticString("bar.test.google.au"));
  bool seen = false;
  // TODO(ctiller): use Activity here, once it's ready.
  TestContext<Arena> context(arena.get());
  auto promise = filter.MakeCallPromise(
      CallArgs{
          ClientMetadataHandle::TestOnlyWrap(&initial_metadata_batch),
          nullptr,
      },
      [&](CallArgs call_args) {
        EXPECT_EQ(call_args.client_initial_metadata
                      ->get_pointer(HttpAuthorityMetadata())
                      ->as_string_view(),
                  "bar.test.google.au");
        seen = true;
        return ArenaPromise<ServerMetadataHandle>(
            [&]() -> Poll<ServerMetadataHandle> {
              return ServerMetadataHandle::TestOnlyWrap(
                  &trailing_metadata_batch);
            });
      });
  auto result = promise();
  EXPECT_TRUE(absl::get_if<ServerMetadataHandle>(&result) != nullptr);
  EXPECT_TRUE(seen);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
