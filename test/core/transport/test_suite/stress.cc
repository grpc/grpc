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

#include "absl/random/random.h"

#include "test/core/transport/test_suite/test.h"

namespace grpc_core {

TRANSPORT_TEST(ManyUnaryRequests) {
  SetServerAcceptor();
  const int kNumRequests = absl::LogUniform<int>(rng(), 10, 100);
  std::list<std::string> call_names;
  auto make_call_name = [&call_names](int i,
                                      absl::string_view suffix) -> const char* {
    call_names.emplace_back(absl::StrCat("call-", i, "-", suffix));
    return call_names.back().c_str();
  };
  std::map<int, std::string> client_messages;
  std::map<int, std::string> server_messages;
  for (int i = 0; i < kNumRequests; i++) {
    auto initiator = CreateCall();
    client_messages[i] = RandomMessage();
    server_messages[i] = RandomMessage();
    SpawnTestSeq(
        initiator, make_call_name(i, "initiator"),
        [initiator, i]() mutable {
          auto md = Arena::MakePooled<ClientMetadata>(GetContext<Arena>());
          md->Set(HttpPathMetadata(),
                  Slice::FromCopiedString(std::to_string(i)));
          return initiator.PushClientInitialMetadata(std::move(md));
        },
        [initiator, i, &client_messages](StatusFlag status) mutable {
          EXPECT_TRUE(status.ok());
          return initiator.PushMessage(Arena::MakePooled<Message>(
              SliceBuffer(Slice::FromCopiedString(client_messages[i])), 0));
        },
        [initiator](StatusFlag status) mutable {
          EXPECT_TRUE(status.ok());
          initiator.FinishSends();
          return initiator.PullServerInitialMetadata();
        },
        [initiator](
            ValueOrFailure<absl::optional<ServerMetadataHandle>> md) mutable {
          EXPECT_TRUE(md.ok());
          EXPECT_TRUE(md.value().has_value());
          EXPECT_EQ(*md.value().value()->get_pointer(ContentTypeMetadata()),
                    ContentTypeMetadata::kApplicationGrpc);
          return initiator.PullMessage();
        },
        [initiator, i,
         &server_messages](NextResult<MessageHandle> msg) mutable {
          EXPECT_TRUE(msg.has_value());
          EXPECT_EQ(msg.value()->payload()->JoinIntoString(),
                    server_messages[i]);
          return initiator.PullMessage();
        },
        [initiator](NextResult<MessageHandle> msg) mutable {
          EXPECT_FALSE(msg.has_value());
          EXPECT_FALSE(msg.cancelled());
          return initiator.PullServerTrailingMetadata();
        },
        [initiator](ValueOrFailure<ServerMetadataHandle> md) mutable {
          EXPECT_TRUE(md.ok());
          EXPECT_EQ(*md.value()->get_pointer(GrpcStatusMetadata()),
                    GRPC_STATUS_UNIMPLEMENTED);
          return Empty{};
        });
  }
  for (int i = 0; i < kNumRequests; i++) {
    auto handler = TickUntilServerCall();
    auto this_call_index = std::make_shared<int>(-1);
    SpawnTestSeq(
        handler, make_call_name(i, "handler"),
        [handler]() mutable { return handler.PullClientInitialMetadata(); },
        [handler,
         this_call_index](ValueOrFailure<ServerMetadataHandle> md) mutable {
          EXPECT_TRUE(md.ok());
          EXPECT_TRUE(absl::SimpleAtoi(
              md.value()->get_pointer(HttpPathMetadata())->as_string_view(),
              &*this_call_index));
          return handler.PullMessage();
        },
        [handler, this_call_index,
         &client_messages](NextResult<MessageHandle> msg) mutable {
          EXPECT_TRUE(msg.has_value());
          EXPECT_EQ(msg.value()->payload()->JoinIntoString(),
                    client_messages[*this_call_index]);
          return handler.PullMessage();
        },
        [handler](NextResult<MessageHandle> msg) mutable {
          EXPECT_FALSE(msg.has_value());
          EXPECT_FALSE(msg.cancelled());
          auto md = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
          md->Set(ContentTypeMetadata(), ContentTypeMetadata::kApplicationGrpc);
          return handler.PushServerInitialMetadata(std::move(md));
        },
        [handler, this_call_index,
         &server_messages](StatusFlag result) mutable {
          EXPECT_TRUE(result.ok());
          return handler.PushMessage(Arena::MakePooled<Message>(
              SliceBuffer(
                  Slice::FromCopiedString(server_messages[*this_call_index])),
              0));
        },
        [handler](StatusFlag result) mutable {
          EXPECT_TRUE(result.ok());
          auto md = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
          md->Set(GrpcStatusMetadata(), GRPC_STATUS_UNIMPLEMENTED);
          return handler.PushServerTrailingMetadata(std::move(md));
        },
        [handler](StatusFlag result) mutable {
          EXPECT_TRUE(result.ok());
          return Empty{};
        });
  }
  WaitForAllPendingWork();
}

}  // namespace grpc_core
