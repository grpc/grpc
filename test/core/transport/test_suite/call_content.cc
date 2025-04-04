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

#include "gmock/gmock.h"
#include "test/core/transport/test_suite/transport_test.h"

using testing::UnorderedElementsAreArray;

namespace grpc_core {

namespace {
class LoweringEncoder {
 public:
  std::vector<std::pair<std::string, std::string>> Take() {
    return std::move(metadata_);
  }

  void Encode(const Slice& key, const Slice& value) {
    metadata_.emplace_back(key.as_string_view(), value.as_string_view());
  }

  template <typename Which>
  void Encode(Which, const typename Which::ValueType& value) {
    metadata_.emplace_back(Which::key(), Which::Encode(value).as_string_view());
  }

 private:
  std::vector<std::pair<std::string, std::string>> metadata_;
};

std::vector<std::pair<std::string, std::string>> LowerMetadata(
    const grpc_metadata_batch& metadata) {
  LoweringEncoder encoder;
  metadata.Encode(&encoder);
  return encoder.Take();
}

void FillMetadata(const std::vector<std::pair<std::string, std::string>>& md,
                  grpc_metadata_batch& out) {
  for (const auto& p : md) {
    out.Append(p.first, Slice::FromCopiedString(p.second),
               [&](absl::string_view error, const Slice& value) {
                 Crash(absl::StrCat(
                     "Failed to parse metadata for '", p.first, "': ", error,
                     " value=", absl::CEscape(value.as_string_view())));
               });
  }
}

}  // namespace

TRANSPORT_TEST(UnaryWithSomeContent) {
  SetServerCallDestination();
  const auto client_initial_metadata = RandomMetadata();
  const auto server_initial_metadata = RandomMetadata();
  const auto server_trailing_metadata = RandomMetadata();
  const auto client_payload = RandomMessage();
  const auto server_payload = RandomMessage();
  auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
  FillMetadata(client_initial_metadata, *md);
  auto initiator = CreateCall(std::move(md));
  SpawnTestSeq(
      initiator, "initiator",
      [&]() mutable {
        return initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString(client_payload)), 0));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        initiator.FinishSends();
        return initiator.PullServerInitialMetadata();
      },
      [&](ValueOrFailure<std::optional<ServerMetadataHandle>> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_TRUE(md.value().has_value());
        EXPECT_THAT(LowerMetadata(***md),
                    UnorderedElementsAreArray(server_initial_metadata));
        return initiator.PullMessage();
      },
      [&](ServerToClientNextMessage msg) {
        EXPECT_TRUE(msg.ok());
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value().payload()->JoinIntoString(), server_payload);
        return initiator.PullMessage();
      },
      [&](ServerToClientNextMessage msg) {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());
        return initiator.PullServerTrailingMetadata();
      },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(LowerMetadata(**md),
                    UnorderedElementsAreArray(server_trailing_metadata));
      });
  auto handler = TickUntilServerCall();
  SpawnTestSeq(
      handler, "handler", [&] { return handler.PullClientInitialMetadata(); },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(LowerMetadata(**md),
                    UnorderedElementsAreArray(client_initial_metadata));
        return handler.PullMessage();
      },
      [&](ClientToServerNextMessage msg) {
        EXPECT_TRUE(msg.ok());
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value().payload()->JoinIntoString(), client_payload);
        return handler.PullMessage();
      },
      [&](ClientToServerNextMessage msg) {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());
        auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
        FillMetadata(server_initial_metadata, *md);
        return handler.PushServerInitialMetadata(std::move(md));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return handler.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString(server_payload)), 0));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
        FillMetadata(server_trailing_metadata, *md);
        handler.PushServerTrailingMetadata(std::move(md));
      });
  WaitForAllPendingWork();
}

TEST(TransportTest, UnaryWithSomeContentRegression1) {
  UnaryWithSomeContent(ParseTestProto(
      R"pb(
        event_engine_actions {
          run_delay: 9223372036854775807
          run_delay: 16903226036976823336
          assign_ports: 4294967295
          connections { write_size: 0 }
        }
        config_vars { verbosity: "debug" dns_resolver: "" experiments: "" }
        rng: 1)pb"));
}

}  // namespace grpc_core
