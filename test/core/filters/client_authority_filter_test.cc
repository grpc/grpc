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

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/call/metadata.h"
#include "src/core/lib/channel/channel_args.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/filter_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class ClientAuthorityFilterTest : public FilterTest {
 protected:
  using FilterTest::FilterTest;

  absl::Status InitWithDefaultAuthority(absl::string_view default_authority) {
    return InitChannel<ClientAuthorityFilter>(
        ChannelArgs().Set(GRPC_ARG_DEFAULT_AUTHORITY, default_authority));
  }
};

// Without GRPC_ARG_DEFAULT_AUTHORITY the filter cannot be constructed; the
// failure surfaces as a non-OK status from InitChannel().
FILTER_TEST(ClientAuthorityFilterTest, DefaultFails) {
  EXPECT_FALSE(InitChannel<ClientAuthorityFilter>().ok());
}

FILTER_TEST(ClientAuthorityFilterTest, WithArgSucceeds) {
  EXPECT_EQ(InitWithDefaultAuthority("foo.test.google.au"), absl::OkStatus());
}

// The authority arg must be a string: an int-valued arg fails construction.
FILTER_TEST(ClientAuthorityFilterTest, NonStringArgFails) {
  EXPECT_FALSE(InitChannel<ClientAuthorityFilter>(
                   ChannelArgs().Set(GRPC_ARG_DEFAULT_AUTHORITY, 123))
                   .ok());
}

// When the client omits :authority, the filter fills it in from the channel
// arg, and the metadata seen at the server carries it.
FILTER_TEST(ClientAuthorityFilterTest, SetsAuthority) {
  ASSERT_TRUE(InitWithDefaultAuthority("foo.test.google.au").ok());
  auto [initiator, handler] = StartCallForFilter(NewClientMetadata());

  ValueOrFailure<ClientMetadataHandle> client_initial_metadata =
      PullClientInitialMetadata(handler);
  ASSERT_TRUE(client_initial_metadata.ok());
  EXPECT_THAT(**client_initial_metadata,
              HasMetadataKeyValue(":authority", "foo.test.google.au"));

  PushServerTrailingMetadata(handler, ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingMetadata().ok());
  WaitForAllPendingWork();
}

// When the client already set :authority, the filter must not override it.
FILTER_TEST(ClientAuthorityFilterTest, DoesNotOverrideAuthority) {
  ASSERT_TRUE(InitWithDefaultAuthority("foo.test.google.au").ok());
  auto [initiator, handler] = StartCallForFilter(
      NewClientMetadata({{":authority", "bar.test.google.au"}}));

  ValueOrFailure<ClientMetadataHandle> client_initial_metadata =
      PullClientInitialMetadata(handler);
  ASSERT_TRUE(client_initial_metadata.ok());
  EXPECT_THAT(**client_initial_metadata,
              HasMetadataKeyValue(":authority", "bar.test.google.au"));

  PushServerTrailingMetadata(handler, ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingMetadata().ok());
  WaitForAllPendingWork();
}

}  // namespace grpc_core
