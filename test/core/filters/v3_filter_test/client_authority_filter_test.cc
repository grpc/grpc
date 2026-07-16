// Copyright 2026 gRPC authors.
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

// M1 gate test: proves FilterTestV3 drives a real v3 filter on the genuine
// CallSpine. ClientAuthorityFilter should stamp the :authority header from the
// GRPC_ARG_DEFAULT_AUTHORITY channel arg when the client did not set one, and
// leave a client-provided :authority untouched.

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include <string>

#include "src/core/call/metadata.h"
#include "src/core/ext/filters/http/client_authority_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "test/core/filters/v3_filter_test/v3_filter_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {

namespace {
ChannelArgs WithDefaultAuthority(absl::string_view authority) {
  return ChannelArgs().Set(GRPC_ARG_DEFAULT_AUTHORITY, authority);
}
}  // namespace

// Build failures surface as a non-OK status from Build().
FILTER_TEST_V3(RejectsMissingDefaultAuthority) {
  EXPECT_FALSE(Add<ClientAuthorityFilter>().Build(ChannelArgs()).ok());
}

// When the client omits :authority, the filter fills it from the channel arg,
// and the (post-filter) metadata seen at the server carries it.
FILTER_TEST_V3(StampsDefaultAuthorityWhenAbsent) {
  ASSERT_TRUE(Add<ClientAuthorityFilter>()
                  .Build(WithDefaultAuthority("foo.test"))
                  .ok());

  auto [initiator, handler] = StartCall(Arena::MakePooledForOverwrite<ClientMetadata>());
  SpawnTestSeq(
      initiator, "client",
      [initiator = initiator]() mutable { return initiator.PullServerTrailingMetadata(); },
      [](ValueOrFailure<ServerMetadataHandle> md) { EXPECT_TRUE(md.ok()); });

  SpawnTestSeq(
      handler, "server",
      [handler = handler]() mutable { return handler.PullClientInitialMetadata(); },
      [handler = handler](ValueOrFailure<ClientMetadataHandle> md) mutable {
        ASSERT_TRUE(md.ok());
        std::string authority;
        EXPECT_EQ((**md).GetStringValue(":authority", &authority), "foo.test");
        handler.PushServerTrailingMetadata(
            ServerMetadataFromStatus(GRPC_STATUS_OK));
      });

  WaitForAllPendingWork();
}

// When the client already set :authority, the filter must not override it.
FILTER_TEST_V3(PreservesClientProvidedAuthority) {
  ASSERT_TRUE(Add<ClientAuthorityFilter>()
                  .Build(WithDefaultAuthority("foo.test"))
                  .ok());

  auto client_initial_metadata = Arena::MakePooledForOverwrite<ClientMetadata>();
  client_initial_metadata->Set(HttpAuthorityMetadata(),
                               Slice::FromStaticString("bar.test"));
  auto [initiator, handler] = StartCall(std::move(client_initial_metadata));
  SpawnTestSeq(
      initiator, "client",
      [initiator = initiator]() mutable { return initiator.PullServerTrailingMetadata(); },
      [](ValueOrFailure<ServerMetadataHandle> md) { EXPECT_TRUE(md.ok()); });

  SpawnTestSeq(
      handler, "server",
      [handler = handler]() mutable { return handler.PullClientInitialMetadata(); },
      [handler = handler](ValueOrFailure<ClientMetadataHandle> md) mutable {
        ASSERT_TRUE(md.ok());
        std::string authority;
        EXPECT_EQ((**md).GetStringValue(":authority", &authority), "bar.test");
        handler.PushServerTrailingMetadata(
            ServerMetadataFromStatus(GRPC_STATUS_OK));
      });

  WaitForAllPendingWork();
}

}  // namespace grpc_core
