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

#include <stddef.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/promise/test_context.h"

// TODO(roth): Need to add a lot more tests here.  I created this file
// as part of adding a feature, and I added tests only for the feature I
// was adding.  When we have time, we need to go back and write
// comprehensive tests for all of the functionality in the filter.

namespace grpc_core {
namespace {

class ClientAuthFilterTest : public ::testing::Test {
 protected:
  class FailCallCreds : public grpc_call_credentials {
   public:
    explicit FailCallCreds(absl::Status status)
        : grpc_call_credentials(GRPC_SECURITY_NONE),
          status_(std::move(status)) {}

    UniqueTypeName type() const override {
      static UniqueTypeName::Factory kFactory("FailCallCreds");
      return kFactory.Create();
    }

    ArenaPromise<absl::StatusOr<ClientMetadataHandle>> GetRequestMetadata(
        ClientMetadataHandle /*initial_metadata*/,
        const GetRequestMetadataArgs* /*args*/) override {
      return Immediate<absl::StatusOr<ClientMetadataHandle>>(status_);
    }

    int cmp_impl(const grpc_call_credentials* other) const override {
      return QsortCompare(
          status_.ToString(),
          static_cast<const FailCallCreds*>(other)->status_.ToString());
    }

   private:
    absl::Status status_;
  };

  ClientAuthFilterTest()
      : memory_allocator_(
            ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
                "test")),
        arena_(MakeScopedArena(1024, &memory_allocator_)),
        initial_metadata_batch_(arena_.get()),
        trailing_metadata_batch_(arena_.get()),
        target_(Slice::FromStaticString("localhost:1234")),
        channel_creds_(grpc_fake_transport_security_credentials_create()) {
    initial_metadata_batch_.Set(HttpAuthorityMetadata(), target_.Ref());
  }

  ~ClientAuthFilterTest() override {
    for (size_t i = 0; i < GRPC_CONTEXT_COUNT; ++i) {
      if (call_context_[i].destroy != nullptr) {
        call_context_[i].destroy(call_context_[i].value);
      }
    }
  }

  ChannelArgs MakeChannelArgs(absl::Status status_for_call_creds) {
    ChannelArgs args;
    auto security_connector = channel_creds_->create_security_connector(
        status_for_call_creds.ok()
            ? nullptr
            : MakeRefCounted<FailCallCreds>(std::move(status_for_call_creds)),
        std::string(target_.as_string_view()).c_str(), &args);
    auto auth_context = MakeRefCounted<grpc_auth_context>(nullptr);
    absl::string_view security_level = "TSI_SECURITY_NONE";
    auth_context->add_property(GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME,
                               security_level.data(), security_level.size());
    return args.SetObject(std::move(security_connector))
        .SetObject(std::move(auth_context));
  }

  MemoryAllocator memory_allocator_;
  ScopedArenaPtr arena_;
  grpc_metadata_batch initial_metadata_batch_;
  grpc_metadata_batch trailing_metadata_batch_;
  Slice target_;
  RefCountedPtr<grpc_channel_credentials> channel_creds_;
  grpc_call_context_element call_context_[GRPC_CONTEXT_COUNT];
};

TEST_F(ClientAuthFilterTest, CreateFailsWithoutRequiredChannelArgs) {
  EXPECT_FALSE(
      ClientAuthFilter::Create(ChannelArgs(), ChannelFilter::Args()).ok());
}

TEST_F(ClientAuthFilterTest, CreateSucceeds) {
  auto filter = ClientAuthFilter::Create(MakeChannelArgs(absl::OkStatus()),
                                         ChannelFilter::Args());
  EXPECT_TRUE(filter.ok()) << filter.status();
}

TEST_F(ClientAuthFilterTest, CallCredsFails) {
  auto filter = ClientAuthFilter::Create(
      MakeChannelArgs(absl::UnauthenticatedError("access denied")),
      ChannelFilter::Args());
  // TODO(ctiller): use Activity here, once it's ready.
  TestContext<Arena> context(arena_.get());
  TestContext<grpc_call_context_element> promise_call_context(call_context_);
  auto promise = filter->MakeCallPromise(
      CallArgs{ClientMetadataHandle(&initial_metadata_batch_,
                                    Arena::PooledDeleter(nullptr)),
               nullptr, nullptr, nullptr},
      [&](CallArgs /*call_args*/) {
        return ArenaPromise<ServerMetadataHandle>(
            [&]() -> Poll<ServerMetadataHandle> {
              return ServerMetadataHandle(&trailing_metadata_batch_,
                                          Arena::PooledDeleter(nullptr));
            });
      });
  auto result = promise();
  ServerMetadataHandle* server_metadata = result.value_if_ready();
  ASSERT_TRUE(server_metadata != nullptr);
  auto status_md = (*server_metadata)->get(GrpcStatusMetadata());
  ASSERT_TRUE(status_md.has_value());
  EXPECT_EQ(*status_md, GRPC_STATUS_UNAUTHENTICATED);
  const Slice* message_md =
      (*server_metadata)->get_pointer(GrpcMessageMetadata());
  ASSERT_TRUE(message_md != nullptr);
  EXPECT_EQ(message_md->as_string_view(), "access denied");
}

TEST_F(ClientAuthFilterTest, RewritesInvalidStatusFromCallCreds) {
  auto filter = ClientAuthFilter::Create(
      MakeChannelArgs(absl::AbortedError("nope")), ChannelFilter::Args());
  // TODO(ctiller): use Activity here, once it's ready.
  TestContext<Arena> context(arena_.get());
  TestContext<grpc_call_context_element> promise_call_context(call_context_);
  auto promise = filter->MakeCallPromise(
      CallArgs{ClientMetadataHandle(&initial_metadata_batch_,
                                    Arena::PooledDeleter(nullptr)),
               nullptr, nullptr, nullptr},
      [&](CallArgs /*call_args*/) {
        return ArenaPromise<ServerMetadataHandle>(
            [&]() -> Poll<ServerMetadataHandle> {
              return ServerMetadataHandle(&trailing_metadata_batch_,
                                          Arena::PooledDeleter(nullptr));
            });
      });
  auto result = promise();
  ServerMetadataHandle* server_metadata = result.value_if_ready();
  ASSERT_TRUE(server_metadata != nullptr);
  auto status_md = (*server_metadata)->get(GrpcStatusMetadata());
  ASSERT_TRUE(status_md.has_value());
  EXPECT_EQ(*status_md, GRPC_STATUS_INTERNAL);
  const Slice* message_md =
      (*server_metadata)->get_pointer(GrpcMessageMetadata());
  ASSERT_TRUE(message_md != nullptr);
  EXPECT_EQ(message_md->as_string_view(),
            "Illegal status code from call credentials; original status: "
            "ABORTED: nope");
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int retval = RUN_ALL_TESTS();
  grpc_shutdown();
  return retval;
}
