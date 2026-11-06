//
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
//

#include "src/core/load_balancing/autosharding/autosharding_assignment.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/text_format.h"
#include "proto/autosharding/v1/autosharding.pb.h"
#include "proto/autosharding/v1/autosharding.upb.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/test_util/test_config.h"
#include "upb/mem/arena.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

using ::google::cloud::autosharding::v1::WatchShardingAssignmentResponse;
using ::testing::ElementsAre;
using ::testing::FieldsAre;
using ::testing::Optional;
using Result = AutoshardingAssignmentBuilder::Result;

// Matches an absl::StatusOr<> that holds a value that matches value_matcher.
MATCHER_P(IsOkAndHolds, value_matcher, "") {
  if (!arg.ok()) {
    *result_listener << "status is " << arg.status();
    return false;
  }
  return ::testing::ExplainMatchResult(value_matcher, *arg, result_listener);
}

// Matches an absl::StatusOr<> whose status is equal to status.
MATCHER_P(HasStatus, status, "") {
  *result_listener << "status is " << arg.status();
  return arg.status() == status;
}

auto IsSlice(absl::string_view end_key, std::vector<size_t> endpoints,
             bool fallback_disabled) {
  return FieldsAre(end_key, std::move(endpoints), fallback_disabled);
}

// Matches a RefCountedPtr<const AutoshardingAssignment>.
MATCHER_P2(IsAssignment, slices_matcher, endpoint_names_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "assignment is null";
    return false;
  }
  return ::testing::ExplainMatchResult(slices_matcher, arg->slices,
                                       result_listener) &&
         ::testing::ExplainMatchResult(endpoint_names_matcher,
                                       arg->endpoint_names, result_listener);
}

// Matches the result of ProcessResponse() for an accepted assignment.
template <typename AssignmentMatcher>
auto IsAccepted(int64_t generation, AssignmentMatcher assignment,
                absl::string_view issues) {
  return Optional(
      FieldsAre(generation, IsOkAndHolds(assignment), std::string(issues)));
}

// Matches the result of ProcessResponse() for a rejected assignment.
auto IsRejected(int64_t generation, absl::Status status) {
  return Optional(FieldsAre(generation, HasStatus(std::move(status)), ""));
}

class AutoshardingAssignmentBuilderTest : public ::testing::Test {
 protected:
  // Parses textproto as a WatchShardingAssignmentResponse with the protobuf
  // library, and passes it to the builder as a upb message.
  std::optional<Result> ProcessResponse(absl::string_view textproto) {
    WatchShardingAssignmentResponse proto;
    EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
        std::string(textproto), &proto))
        << textproto;
    const std::string serialized = proto.SerializeAsString();
    const auto* response =
        google_cloud_autosharding_v1_WatchShardingAssignmentResponse_parse(
            serialized.data(), serialized.size(), arena_.ptr());
    EXPECT_NE(response, nullptr);
    return builder_.ProcessResponseMessage(response);
  }

  upb::Arena arena_;
  AutoshardingAssignmentBuilder builder_;
};

constexpr absl::string_view kSingleSliceChunk = R"pb(
  chunk {
    slice_assignments {
      slice { start_key: "" }
      endpoints { endpoint_index: 0 }
    }
    endpoints { endpoint: "host0" }
  }
)pb";

TEST_F(AutoshardingAssignmentBuilderTest, SingleSliceCoversKeyspace) {
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "" }
                  endpoints { endpoint_index: 0 }
                  endpoints { endpoint_index: 1 }
                }
                endpoints { endpoint: "host0" }
                endpoints { endpoint: "host1" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsAccepted(/*generation=*/1,
                         IsAssignment(ElementsAre(IsSlice("", {0, 1}, false)),
                                      ElementsAre("host0", "host1")),
                         /*issues=*/""));
}

TEST_F(AutoshardingAssignmentBuilderTest, MissingSliceCoversKeyspace) {
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments { endpoints { endpoint_index: 0 } }
                endpoints { endpoint: "host0" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsAccepted(1,
                         IsAssignment(ElementsAre(IsSlice("", {0}, false)),
                                      ElementsAre("host0")),
                         ""));
}

TEST_F(AutoshardingAssignmentBuilderTest, SlicesAreSorted) {
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "m" }
                  endpoints { endpoint_index: 1 }
                }
                slice_assignments {
                  slice { start_key: "" end_key: "m" }
                  endpoints { endpoint_index: 0 }
                }
                endpoints { endpoint: "host0" }
                endpoints { endpoint: "host1" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsAccepted(1,
                         IsAssignment(ElementsAre(IsSlice("m", {0}, false),
                                                  IsSlice("", {1}, false)),
                                      ElementsAre("host0", "host1")),
                         ""));
}

TEST_F(AutoshardingAssignmentBuilderTest,
       EndpointNamesConcatenatedInChunkOrder) {
  // Slices refer to endpoints in other chunks, and slices are spread across
  // chunks.
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "" end_key: "m" }
                  endpoints { endpoint_index: 2 }
                }
                endpoints { endpoint: "host0" }
              }
            )pb"),
            std::nullopt);
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "m" }
                  endpoints { endpoint_index: 0 }
                }
                endpoints { endpoint: "host1" }
                endpoints { endpoint: "host2" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsAccepted(1,
                         IsAssignment(ElementsAre(IsSlice("m", {2}, false),
                                                  IsSlice("", {0}, false)),
                                      ElementsAre("host0", "host1", "host2")),
                         ""));
}

TEST_F(AutoshardingAssignmentBuilderTest, EmptyEndpointListDisablesFallback) {
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments { slice { start_key: "" end_key: "a" } }
                slice_assignments {
                  slice { start_key: "a" end_key: "b" }
                  endpoints { endpoint_index: 0 }
                }
                slice_assignments { slice { start_key: "b" } }
                endpoints { endpoint: "host0" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsAccepted(1,
                         IsAssignment(ElementsAre(IsSlice("a", {}, true),
                                                  IsSlice("b", {0}, false),
                                                  IsSlice("", {}, true)),
                                      ElementsAre("host0")),
                         ""));
}

TEST_F(AutoshardingAssignmentBuilderTest,
       GapNextToSliceWithoutEndpointsKeepsFallbackEnabled) {
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments { slice { start_key: "b" end_key: "c" } }
                endpoints { endpoint: "host0" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsAccepted(1,
                         IsAssignment(ElementsAre(IsSlice("b", {}, false),
                                                  IsSlice("c", {}, true),
                                                  IsSlice("", {}, false)),
                                      ElementsAre("host0")),
                         "encountered 2 gap(s) in the assignment"));
}

TEST_F(AutoshardingAssignmentBuilderTest, GapsAreFilled) {
  // Gaps at the start, in the middle, and at the end of the keyspace.
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "b" end_key: "c" }
                  endpoints { endpoint_index: 0 }
                }
                slice_assignments {
                  slice { start_key: "d" end_key: "e" }
                  endpoints { endpoint_index: 0 }
                }
                endpoints { endpoint: "host0" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsAccepted(1,
                         IsAssignment(ElementsAre(IsSlice("b", {}, false),
                                                  IsSlice("c", {0}, false),
                                                  IsSlice("d", {}, false),
                                                  IsSlice("e", {0}, false),
                                                  IsSlice("", {}, false)),
                                      ElementsAre("host0")),
                         "encountered 3 gap(s) in the assignment"));
}

TEST_F(AutoshardingAssignmentBuilderTest, InvalidSlicesBecomeGaps) {
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "" end_key: "b" }
                  endpoints { endpoint_index: 0 }
                }
                # Explicit empty end_key is not +infinity.
                slice_assignments {
                  slice { start_key: "" end_key: "" }
                  endpoints { endpoint_index: 0 }
                }
                # start_key not less than end_key.
                slice_assignments {
                  slice { start_key: "c" end_key: "c" }
                  endpoints { endpoint_index: 0 }
                }
                # endpoint_index out of range.
                slice_assignments {
                  slice { start_key: "b" end_key: "c" }
                  endpoints { endpoint_index: 1 }
                }
                # Negative endpoint_index.
                slice_assignments {
                  slice { start_key: "c" end_key: "d" }
                  endpoints { endpoint_index: -1 }
                }
                slice_assignments {
                  slice { start_key: "d" }
                  endpoints { endpoint_index: 0 }
                }
                endpoints { endpoint: "host0" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsAccepted(1,
                         IsAssignment(ElementsAre(IsSlice("b", {0}, false),
                                                  IsSlice("d", {}, false),
                                                  IsSlice("", {0}, false)),
                                      ElementsAre("host0")),
                         "2 slice(s) with start_key >= end_key; "
                         "2 slice(s) with invalid endpoint indices; "
                         "encountered 1 gap(s) in the assignment"));
}

TEST_F(AutoshardingAssignmentBuilderTest, LongKeysAreDropped) {
  const std::string kLongStartKey(513, 'a');
  const std::string kLongEndKey(513, 'b');
  const std::string kMaxLengthKey(512, 'c');
  EXPECT_EQ(ProcessResponse(absl::StrCat(
                "chunk { ", "slice_assignments { slice { start_key: \"",
                kLongStartKey, "\" } endpoints { endpoint_index: 0 } } ",
                "slice_assignments { slice { start_key: \"\" end_key: \"",
                kLongEndKey, "\" } endpoints { endpoint_index: 0 } } ",
                "slice_assignments { slice { start_key: \"", kMaxLengthKey,
                "\" } endpoints { endpoint_index: 0 } } ",
                "endpoints { endpoint: \"host0\" } }")),
            std::nullopt);
  EXPECT_THAT(
      ProcessResponse("metadata { generation: 1 }"),
      IsAccepted(1,
                 IsAssignment(ElementsAre(IsSlice(kMaxLengthKey, {}, false),
                                          IsSlice("", {0}, false)),
                              ElementsAre("host0")),
                 "1 slice(s) with start_key longer than 512 bytes; "
                 "1 slice(s) with end_key longer than 512 bytes; "
                 "encountered 1 gap(s) in the assignment"));
}

TEST_F(AutoshardingAssignmentBuilderTest, OverlappingSlicesAreAllDropped) {
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "" end_key: "m" }
                  endpoints { endpoint_index: 0 }
                }
                slice_assignments {
                  slice { start_key: "k" end_key: "z" }
                  endpoints { endpoint_index: 1 }
                }
                slice_assignments {
                  slice { start_key: "z" }
                  endpoints { endpoint_index: 1 }
                }
                endpoints { endpoint: "host0" }
                endpoints { endpoint: "host1" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(
      ProcessResponse("metadata { generation: 1 }"),
      IsAccepted(
          1,
          IsAssignment(
              ElementsAre(IsSlice("z", {}, false), IsSlice("", {1}, false)),
              ElementsAre("host0", "host1")),
          "2 overlapping slices; encountered 1 gap(s) in the assignment"));
}

TEST_F(AutoshardingAssignmentBuilderTest,
       ChainedOverlappingSlicesAreAllDropped) {
  // The third slice overlaps the second slice, which overlaps the first
  // slice, even though the third slice does not overlap the first slice.
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "a" end_key: "d" }
                  endpoints { endpoint_index: 0 }
                }
                slice_assignments {
                  slice { start_key: "c" end_key: "f" }
                  endpoints { endpoint_index: 0 }
                }
                slice_assignments {
                  slice { start_key: "e" end_key: "h" }
                  endpoints { endpoint_index: 0 }
                }
                slice_assignments {
                  slice { start_key: "h" }
                  endpoints { endpoint_index: 0 }
                }
                endpoints { endpoint: "host0" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(
      ProcessResponse("metadata { generation: 1 }"),
      IsAccepted(
          1,
          IsAssignment(
              ElementsAre(IsSlice("h", {}, false), IsSlice("", {0}, false)),
              ElementsAre("host0")),
          "3 overlapping slices; encountered 1 gap(s) in the assignment"));
}

TEST_F(AutoshardingAssignmentBuilderTest, ContainedSlicesAreAllDropped) {
  // The first slice contains the next two, which do not overlap each other.
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "" end_key: "z" }
                  endpoints { endpoint_index: 0 }
                }
                slice_assignments {
                  slice { start_key: "b" end_key: "c" }
                  endpoints { endpoint_index: 0 }
                }
                slice_assignments {
                  slice { start_key: "d" end_key: "e" }
                  endpoints { endpoint_index: 0 }
                }
                slice_assignments {
                  slice { start_key: "z" }
                  endpoints { endpoint_index: 0 }
                }
                endpoints { endpoint: "host0" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(
      ProcessResponse("metadata { generation: 1 }"),
      IsAccepted(
          1,
          IsAssignment(
              ElementsAre(IsSlice("z", {}, false), IsSlice("", {0}, false)),
              ElementsAre("host0")),
          "3 overlapping slices; encountered 1 gap(s) in the assignment"));
}

TEST_F(AutoshardingAssignmentBuilderTest, SlicesWithSameStartKeyOverlap) {
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "" end_key: "m" }
                  endpoints { endpoint_index: 0 }
                }
                slice_assignments {
                  slice { start_key: "" }
                  endpoints { endpoint_index: 0 }
                }
                endpoints { endpoint: "host0" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsRejected(1, absl::InvalidArgumentError(
                                "no valid slice assignments in generation 1: "
                                "2 overlapping slices")));
}

TEST_F(AutoshardingAssignmentBuilderTest,
       InfiniteEndKeyBeforeLaterSliceOverlaps) {
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "" end_key: "d" }
                  endpoints { endpoint_index: 0 }
                }
                slice_assignments {
                  slice { start_key: "d" }
                  endpoints { endpoint_index: 0 }
                }
                slice_assignments {
                  slice { start_key: "m" end_key: "z" }
                  endpoints { endpoint_index: 0 }
                }
                endpoints { endpoint: "host0" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(
      ProcessResponse("metadata { generation: 1 }"),
      IsAccepted(
          1,
          IsAssignment(
              ElementsAre(IsSlice("d", {0}, false), IsSlice("", {}, false)),
              ElementsAre("host0")),
          "2 overlapping slices; encountered 1 gap(s) in the assignment"));
}

TEST_F(AutoshardingAssignmentBuilderTest, InvalidSliceDoesNotCauseOverlap) {
  // The second slice has an invalid endpoint index, so it is dropped before
  // the overlap check, and the other slices are kept.
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "" end_key: "m" }
                  endpoints { endpoint_index: 0 }
                }
                slice_assignments {
                  slice { start_key: "k" end_key: "z" }
                  endpoints { endpoint_index: 1 }
                }
                slice_assignments {
                  slice { start_key: "m" }
                  endpoints { endpoint_index: 0 }
                }
                endpoints { endpoint: "host0" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsAccepted(1,
                         IsAssignment(ElementsAre(IsSlice("m", {0}, false),
                                                  IsSlice("", {0}, false)),
                                      ElementsAre("host0")),
                         "1 slice(s) with invalid endpoint indices"));
}

TEST_F(AutoshardingAssignmentBuilderTest, NoChunksIsRejected) {
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsRejected(1, absl::InvalidArgumentError(
                                "received metadata with generation 1, but no "
                                "chunks were received")));
}

TEST_F(AutoshardingAssignmentBuilderTest, NoSlicesIsRejected) {
  EXPECT_EQ(ProcessResponse(R"pb(chunk { endpoints { endpoint: "host0" } })pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsRejected(1, absl::InvalidArgumentError(
                                "no valid slice assignments in generation 1: "
                                "no slice assignments received")));
}

TEST_F(AutoshardingAssignmentBuilderTest, AllSlicesInvalidIsRejected) {
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "b" end_key: "a" }
                  endpoints { endpoint_index: 0 }
                }
                endpoints { endpoint: "host0" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsRejected(1, absl::InvalidArgumentError(
                                "no valid slice assignments in generation 1: "
                                "1 slice(s) with start_key >= end_key")));
}

TEST_F(AutoshardingAssignmentBuilderTest, StaleGeneration) {
  const auto kSingleSliceAssignment =
      IsAssignment(ElementsAre(IsSlice("", {0}, false)), ElementsAre("host0"));
  EXPECT_EQ(ProcessResponse(kSingleSliceChunk), std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 5 }"),
              IsAccepted(5, kSingleSliceAssignment, ""));
  EXPECT_EQ(ProcessResponse(kSingleSliceChunk), std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 5 }"),
              IsRejected(5, absl::InvalidArgumentError(
                                "received generation 5 is not greater than "
                                "the latest generation 5")));
  EXPECT_EQ(ProcessResponse(kSingleSliceChunk), std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 6 }"),
              IsAccepted(6, kSingleSliceAssignment, ""));
}

TEST_F(AutoshardingAssignmentBuilderTest, ZeroGenerationIsStale) {
  EXPECT_EQ(ProcessResponse(kSingleSliceChunk), std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 0 }"),
              IsRejected(0, absl::InvalidArgumentError(
                                "received generation 0 is not greater than "
                                "the latest generation 0")));
}

TEST_F(AutoshardingAssignmentBuilderTest, NegativeGenerationIsStale) {
  EXPECT_EQ(ProcessResponse(kSingleSliceChunk), std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: -1 }"),
              IsRejected(-1, absl::InvalidArgumentError(
                                 "received generation -1 is not greater than "
                                 "the latest generation 0")));
}

TEST_F(AutoshardingAssignmentBuilderTest, TracksLastAcceptedGeneration) {
  EXPECT_EQ(builder_.last_accepted_generation(), 0);
  EXPECT_EQ(ProcessResponse(kSingleSliceChunk), std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 2 }"),
              IsAccepted(2,
                         IsAssignment(ElementsAre(IsSlice("", {0}, false)),
                                      ElementsAre("host0")),
                         ""));
  EXPECT_EQ(builder_.last_accepted_generation(), 2);
  // A rejected assignment does not change the latest accepted generation.
  EXPECT_THAT(ProcessResponse("metadata { generation: 3 }"),
              IsRejected(3, absl::InvalidArgumentError(
                                "received metadata with generation 3, but no "
                                "chunks were received")));
  EXPECT_EQ(builder_.last_accepted_generation(), 2);
}

TEST_F(AutoshardingAssignmentBuilderTest, ResetPartialAssignmentDropsChunks) {
  EXPECT_EQ(ProcessResponse(kSingleSliceChunk), std::nullopt);
  builder_.ResetPartialAssignment();
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsRejected(1, absl::InvalidArgumentError(
                                "received metadata with generation 1, but no "
                                "chunks were received")));
}

TEST_F(AutoshardingAssignmentBuilderTest,
       ResponsesWithoutAssignmentAreIgnored) {
  EXPECT_EQ(ProcessResponse(""), std::nullopt);
  EXPECT_EQ(ProcessResponse("config {}"), std::nullopt);
}

TEST_F(AutoshardingAssignmentBuilderTest, ChunkPrecedesMetadataWhenBothAreSet) {
  // A response with both fields set is handled as a chunk only.
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "" }
                  endpoints { endpoint_index: 0 }
                }
                endpoints { endpoint: "host0" }
              }
              metadata { generation: 1 }
            )pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsAccepted(1,
                         IsAssignment(ElementsAre(IsSlice("", {0}, false)),
                                      ElementsAre("host0")),
                         ""));
}

TEST_F(AutoshardingAssignmentBuilderTest, BuildClearsChunks) {
  const auto kSingleSliceAssignment =
      IsAssignment(ElementsAre(IsSlice("", {0}, false)), ElementsAre("host0"));
  EXPECT_EQ(ProcessResponse(kSingleSliceChunk), std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsAccepted(1, kSingleSliceAssignment, ""));
  EXPECT_THAT(ProcessResponse("metadata { generation: 2 }"),
              IsRejected(2, absl::InvalidArgumentError(
                                "received metadata with generation 2, but no "
                                "chunks were received")));
  EXPECT_EQ(ProcessResponse(kSingleSliceChunk), std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 2 }"),
              IsAccepted(2, kSingleSliceAssignment, ""));
}

TEST_F(AutoshardingAssignmentBuilderTest, StaleBuildClearsChunks) {
  EXPECT_EQ(ProcessResponse(kSingleSliceChunk), std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsAccepted(1,
                         IsAssignment(ElementsAre(IsSlice("", {0}, false)),
                                      ElementsAre("host0")),
                         ""));
  EXPECT_EQ(ProcessResponse(kSingleSliceChunk), std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsRejected(1, absl::InvalidArgumentError(
                                "received generation 1 is not greater than "
                                "the latest generation 1")));
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "" }
                  endpoints { endpoint_index: 0 }
                }
                endpoints { endpoint: "host1" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 2 }"),
              IsAccepted(2,
                         IsAssignment(ElementsAre(IsSlice("", {0}, false)),
                                      ElementsAre("host1")),
                         ""));
}

TEST_F(AutoshardingAssignmentBuilderTest, RejectedBuildClearsChunks) {
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "b" end_key: "a" }
                  endpoints { endpoint_index: 0 }
                }
                endpoints { endpoint: "host0" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsRejected(1, absl::InvalidArgumentError(
                                "no valid slice assignments in generation 1: "
                                "1 slice(s) with start_key >= end_key")));
  EXPECT_EQ(builder_.last_accepted_generation(), 0);
  // The same generation is accepted next, without the rejected chunk.
  EXPECT_EQ(ProcessResponse(R"pb(
              chunk {
                slice_assignments {
                  slice { start_key: "" }
                  endpoints { endpoint_index: 0 }
                }
                endpoints { endpoint: "host1" }
              }
            )pb"),
            std::nullopt);
  EXPECT_THAT(ProcessResponse("metadata { generation: 1 }"),
              IsAccepted(1,
                         IsAssignment(ElementsAre(IsSlice("", {0}, false)),
                                      ElementsAre("host1")),
                         ""));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
