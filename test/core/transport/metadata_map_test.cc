//
// Copyright 2021 gRPC authors.
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
//

#include <stdlib.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/memory_allocator.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

struct EmptyMetadataMap : public MetadataMap<EmptyMetadataMap> {
  using MetadataMap<EmptyMetadataMap>::MetadataMap;
};

struct TimeoutOnlyMetadataMap
    : public MetadataMap<TimeoutOnlyMetadataMap, GrpcTimeoutMetadata> {
  using MetadataMap<TimeoutOnlyMetadataMap, GrpcTimeoutMetadata>::MetadataMap;
};

struct StreamNetworkStateMetadataMap
    : public MetadataMap<StreamNetworkStateMetadataMap,
                         GrpcStreamNetworkState> {
  using MetadataMap<StreamNetworkStateMetadataMap,
                    GrpcStreamNetworkState>::MetadataMap;
};

class MetadataMapTest : public ::testing::Test {
 protected:
  MemoryAllocator memory_allocator_ = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
};

TEST_F(MetadataMapTest, Noop) {
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  EmptyMetadataMap();
}

TEST_F(MetadataMapTest, NoopWithDeadline) {
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  TimeoutOnlyMetadataMap();
}

TEST_F(MetadataMapTest, SimpleOps) {
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  TimeoutOnlyMetadataMap map;
  EXPECT_EQ(map.get_pointer(GrpcTimeoutMetadata()), nullptr);
  EXPECT_EQ(map.get(GrpcTimeoutMetadata()), absl::nullopt);
  map.Set(GrpcTimeoutMetadata(),
          Timestamp::FromMillisecondsAfterProcessEpoch(1234));
  EXPECT_NE(map.get_pointer(GrpcTimeoutMetadata()), nullptr);
  EXPECT_EQ(*map.get_pointer(GrpcTimeoutMetadata()),
            Timestamp::FromMillisecondsAfterProcessEpoch(1234));
  EXPECT_EQ(map.get(GrpcTimeoutMetadata()),
            Timestamp::FromMillisecondsAfterProcessEpoch(1234));
  map.Remove(GrpcTimeoutMetadata());
  EXPECT_EQ(map.get_pointer(GrpcTimeoutMetadata()), nullptr);
  EXPECT_EQ(map.get(GrpcTimeoutMetadata()), absl::nullopt);
}

// Target for MetadataMap::Encode.
// Writes down some string representation of what it receives, so we can
// EXPECT_EQ it later.
class FakeEncoder {
 public:
  std::string output() { return output_; }

  void Encode(const Slice& key, const Slice& value) {
    output_ += absl::StrCat("UNKNOWN METADATUM: key=", key.as_string_view(),
                            " value=", value.as_string_view(), "\n");
  }

  void Encode(GrpcTimeoutMetadata, Timestamp deadline) {
    output_ += absl::StrCat("grpc-timeout: deadline=",
                            deadline.milliseconds_after_process_epoch(), "\n");
  }

 private:
  std::string output_;
};

TEST_F(MetadataMapTest, EmptyEncodeTest) {
  FakeEncoder encoder;
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  TimeoutOnlyMetadataMap map;
  map.Encode(&encoder);
  EXPECT_EQ(encoder.output(), "");
}

TEST_F(MetadataMapTest, TimeoutEncodeTest) {
  FakeEncoder encoder;
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  TimeoutOnlyMetadataMap map;
  map.Set(GrpcTimeoutMetadata(),
          Timestamp::FromMillisecondsAfterProcessEpoch(1234));
  map.Encode(&encoder);
  EXPECT_EQ(encoder.output(), "grpc-timeout: deadline=1234\n");
}

TEST_F(MetadataMapTest, NonEncodableTrait) {
  struct EncoderWithNoTraitEncodeFunctions {
    void Encode(const Slice&, const Slice&) {
      abort();  // should not be called
    }
  };
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  StreamNetworkStateMetadataMap map;
  map.Set(GrpcStreamNetworkState(), GrpcStreamNetworkState::kNotSentOnWire);
  EXPECT_EQ(map.get(GrpcStreamNetworkState()),
            GrpcStreamNetworkState::kNotSentOnWire);
  EncoderWithNoTraitEncodeFunctions encoder;
  map.Encode(&encoder);
  EXPECT_EQ(map.DebugString(), "GrpcStreamNetworkState: not sent on wire");
}

TEST(DebugStringBuilderTest, OneAddAfterRedaction) {
  metadata_detail::DebugStringBuilder b;
  b.AddAfterRedaction(ContentTypeMetadata::key(), "AddValue01");
  EXPECT_EQ(b.TakeOutput(),
            absl::StrCat(ContentTypeMetadata::key(), ": AddValue01"));
}

std::vector<std::string> GetAllowList() {
  return {
      // clang-format off
          std::string(ContentTypeMetadata::key()),
          std::string(EndpointLoadMetricsBinMetadata::key()),
          std::string(GrpcAcceptEncodingMetadata::key()),
          std::string(GrpcEncodingMetadata::key()),
          std::string(GrpcInternalEncodingRequest::key()),
          std::string(GrpcLbClientStatsMetadata::key()),
          std::string(GrpcMessageMetadata::key()),
          std::string(GrpcPreviousRpcAttemptsMetadata::key()),
          std::string(GrpcRetryPushbackMsMetadata::key()),
          std::string(GrpcServerStatsBinMetadata::key()),
          std::string(GrpcStatusMetadata::key()),
          std::string(GrpcTagsBinMetadata::key()),
          std::string(GrpcTimeoutMetadata::key()),
          std::string(GrpcTraceBinMetadata::key()),
          std::string(HostMetadata::key()),
          std::string(HttpAuthorityMetadata::key()),
          std::string(HttpMethodMetadata::key()),
          std::string(HttpPathMetadata::key()),
          std::string(HttpSchemeMetadata::key()),
          std::string(HttpStatusMetadata::key()),
          std::string(LbCostBinMetadata::key()),
          std::string(LbTokenMetadata::key()),
          std::string(TeMetadata::key()),
          std::string(UserAgentMetadata::key()),
          std::string(XEnvoyPeerMetadata::key()),
          std::string(GrpcCallWasCancelled::DebugKey()),
          std::string(GrpcRegisteredMethod::DebugKey()),
          std::string(GrpcStatusContext::DebugKey()),
          std::string(GrpcStatusFromWire::DebugKey()),
          std::string(GrpcStreamNetworkState::DebugKey()),
          std::string(GrpcTarPit::DebugKey()),
          std::string(GrpcTrailersOnly::DebugKey()),
          std::string(PeerString::DebugKey()),
          std::string(WaitForReady::DebugKey())
      // clang-format on
  };
}

TEST(DebugStringBuilderTest, TestAllAllowListed) {
  metadata_detail::DebugStringBuilder builder_add_allow_list;
  const std::vector<std::string> allow_list_keys = GetAllowList();

  for (const std::string& curr_key : allow_list_keys) {
    builder_add_allow_list.AddAfterRedaction(curr_key, curr_key);
  }

  // All values which are allow listed should be added as is.
  EXPECT_EQ(builder_add_allow_list.TakeOutput(),
            "content-type: content-type, "
            "endpoint-load-metrics-bin: endpoint-load-metrics-bin, "
            "grpc-accept-encoding: grpc-accept-encoding, "
            "grpc-encoding: grpc-encoding, "
            "grpc-internal-encoding-request: grpc-internal-encoding-request, "
            "grpclb_client_stats: grpclb_client_stats, "
            "grpc-message: grpc-message, "
            "grpc-previous-rpc-attempts: grpc-previous-rpc-attempts, "
            "grpc-retry-pushback-ms: grpc-retry-pushback-ms, "
            "grpc-server-stats-bin: grpc-server-stats-bin, "
            "grpc-status: grpc-status, "
            "grpc-tags-bin: grpc-tags-bin, "
            "grpc-timeout: grpc-timeout, "
            "grpc-trace-bin: grpc-trace-bin, "
            "host: host, :authority: :authority, "
            ":method: :method, "
            ":path: :path, "
            ":scheme: :scheme, "
            ":status: :status, "
            "lb-cost-bin: lb-cost-bin, "
            "lb-token: lb-token, "
            "te: te, "
            "user-agent: user-agent, "
            "x-envoy-peer-metadata: x-envoy-peer-metadata, "
            "GrpcCallWasCancelled: GrpcCallWasCancelled, "
            "GrpcRegisteredMethod: GrpcRegisteredMethod, "
            "GrpcStatusContext: GrpcStatusContext, "
            "GrpcStatusFromWire: GrpcStatusFromWire, "
            "GrpcStreamNetworkState: GrpcStreamNetworkState, "
            "GrpcTarPit: GrpcTarPit, "
            "GrpcTrailersOnly: GrpcTrailersOnly, "
            "PeerString: PeerString, "
            "WaitForReady: WaitForReady");
}

TEST(DebugStringBuilderTest, TestAllRedacted) {
  metadata_detail::DebugStringBuilder builder_add_redacted;
  const std::vector<std::string> allow_list_keys = GetAllowList();

  for (const std::string& curr_key : allow_list_keys) {
    builder_add_redacted.AddAfterRedaction(curr_key + "1234", curr_key);
  }

  // All values which are not allow listed should be redacted
  std::vector<std::string> redacted_output =
      absl::StrSplit(builder_add_redacted.TakeOutput(), ',');
  int i = 0;
  for (std::string& curr_row : redacted_output) {
    std::string redacted_str = absl::StrCat(
        allow_list_keys[i++].size(), " bytes redacted by allow listing.");
    EXPECT_EQ(absl::StrContains(curr_row, redacted_str), true);
  }
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
};
