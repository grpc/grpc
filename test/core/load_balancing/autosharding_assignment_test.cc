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

#include "proto/autosharding/v1/autosharding.upb.h"
#include "src/core/util/upb_utils.h"
#include "test/core/test_util/test_config.h"
#include "upb/mem/arena.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {
namespace {

using ::testing::ElementsAre;
using Outcome = AutoshardingAssignmentBuilder::Outcome;
using Result = AutoshardingAssignmentBuilder::Result;

// Matches an AutoshardingAssignment::Slice.
auto SliceIs(std::string end_key, std::vector<size_t> endpoints,
             bool fallback_disabled) {
  return ::testing::FieldsAre(std::move(end_key), std::move(endpoints),
                              fallback_disabled);
}

// Describes one SliceAssignment message for the tests.
struct TestSlice {
  std::string start_key;
  // Not set for +infinity.
  std::optional<std::string> end_key;
  std::vector<int32_t> endpoint_indices;
  // If false, the SliceAssignment message has no Slice.
  bool has_slice = true;
};

class AutoshardingAssignmentBuilderTest : public ::testing::Test {
 protected:
  // Adds one AssignmentChunk message to builder_.  The chunk borrows the
  // strings in slices and endpoint_names, which outlive the call.
  void AddChunk(const std::vector<TestSlice>& slices,
                const std::vector<std::string>& endpoint_names) {
    auto* chunk =
        google_cloud_autosharding_v1_AssignmentChunk_new(arena_.ptr());
    for (const TestSlice& test_slice : slices) {
      auto* slice_assignment =
          google_cloud_autosharding_v1_AssignmentChunk_add_slice_assignments(
              chunk, arena_.ptr());
      if (test_slice.has_slice) {
        auto* slice =
            google_cloud_autosharding_v1_SliceAssignment_mutable_slice(
                slice_assignment, arena_.ptr());
        google_cloud_autosharding_v1_Slice_set_start_key(
            slice, StdStringToUpbString(test_slice.start_key));
        if (test_slice.end_key.has_value()) {
          google_cloud_autosharding_v1_Slice_set_end_key(
              slice, StdStringToUpbString(*test_slice.end_key));
        }
      }
      for (int32_t index : test_slice.endpoint_indices) {
        auto* endpoint =
            google_cloud_autosharding_v1_SliceAssignment_add_endpoints(
                slice_assignment, arena_.ptr());
        google_cloud_autosharding_v1_PerSliceEndpointState_set_endpoint_index(
            endpoint, index);
      }
    }
    for (const std::string& name : endpoint_names) {
      auto* endpoint =
          google_cloud_autosharding_v1_AssignmentChunk_add_endpoints(
              chunk, arena_.ptr());
      google_cloud_autosharding_v1_EndpointState_set_endpoint(
          endpoint, StdStringToUpbString(name));
    }
    builder_.AddChunk(chunk);
  }

  // Calls builder_.Build() with last_accepted_generation_, and stores the
  // generation if it is accepted, as the autosharding client does.
  Result Build(int64_t generation) {
    Result result = builder_.Build(generation, last_accepted_generation_);
    if (result.outcome == Outcome::kAccepted) {
      last_accepted_generation_ = generation;
    }
    return result;
  }

  upb::Arena arena_;
  AutoshardingAssignmentBuilder builder_;
  int64_t last_accepted_generation_ = 0;
};

TEST_F(AutoshardingAssignmentBuilderTest, SingleSliceCoversKeyspace) {
  AddChunk({{"", std::nullopt, {0, 1}}}, {"host0", "host1"});
  Result result = Build(/*generation=*/1);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  EXPECT_EQ(result.error_message, "");
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(result.assignment->endpoint_names, ElementsAre("host0", "host1"));
  EXPECT_THAT(result.assignment->slices,
              ElementsAre(SliceIs("", {0, 1}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, MissingSliceCoversKeyspace) {
  AddChunk({{"", std::nullopt, {0}, /*has_slice=*/false}}, {"host0"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  EXPECT_EQ(result.error_message, "");
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(result.assignment->slices, ElementsAre(SliceIs("", {0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, SlicesAreSorted) {
  AddChunk({{"m", std::nullopt, {1}}, {"", "m", {0}}}, {"host0", "host1"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  EXPECT_EQ(result.error_message, "");
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(result.assignment->slices,
              ElementsAre(SliceIs("m", {0}, false), SliceIs("", {1}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest,
       EndpointNamesConcatenatedInChunkOrder) {
  // Slices refer to endpoints in later chunks, and slices are spread across
  // chunks.
  AddChunk({{"", "m", {2}}}, {"host0"});
  AddChunk({{"m", std::nullopt, {0}}}, {"host1", "host2"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(result.assignment->endpoint_names,
              ElementsAre("host0", "host1", "host2"));
  EXPECT_THAT(result.assignment->slices,
              ElementsAre(SliceIs("m", {2}, false), SliceIs("", {0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, DuplicateEndpointIndicesAreKept) {
  AddChunk({{"", std::nullopt, {0, 0}}}, {"host0"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(result.assignment->slices,
              ElementsAre(SliceIs("", {0, 0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, EmptyEndpointListDisablesFallback) {
  AddChunk({{"", "a", {}}, {"a", "b", {0}}, {"b", std::nullopt, {}}},
           {"host0"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  EXPECT_EQ(result.error_message, "");
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(result.assignment->slices,
              ElementsAre(SliceIs("a", {}, true), SliceIs("b", {0}, false),
                          SliceIs("", {}, true)));
}

TEST_F(AutoshardingAssignmentBuilderTest,
       GapNextToSliceWithoutEndpointsKeepsFallbackEnabled) {
  // The gap slices allow fallback, but the slice that the server sent with
  // no endpoints does not.
  AddChunk({{"b", "c", {}}}, {"host0"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  EXPECT_EQ(result.error_message, "encountered 2 gap(s) in the assignment");
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(result.assignment->slices,
              ElementsAre(SliceIs("b", {}, false), SliceIs("c", {}, true),
                          SliceIs("", {}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, GapsAreFilled) {
  // Gaps at the start, in the middle, and at the end of the keyspace.
  AddChunk({{"b", "c", {0}}, {"d", "e", {0}}}, {"host0"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  EXPECT_EQ(result.error_message, "encountered 3 gap(s) in the assignment");
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(result.assignment->slices,
              ElementsAre(SliceIs("b", {}, false), SliceIs("c", {0}, false),
                          SliceIs("d", {}, false), SliceIs("e", {0}, false),
                          SliceIs("", {}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, InvalidSlicesBecomeGaps) {
  AddChunk({{"", "b", {0}},
            // Explicit empty end_key is not +infinity.
            {"", "", {0}},
            // start_key not less than end_key.
            {"c", "c", {0}},
            // endpoint_index out of range.
            {"b", "c", {1}},
            // Negative endpoint_index.
            {"c", "d", {-1}},
            {"d", std::nullopt, {0}}},
           {"host0"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  EXPECT_EQ(result.error_message,
            "2 slice(s) with start_key >= end_key; "
            "2 slice(s) with invalid endpoint indices; "
            "encountered 1 gap(s) in the assignment");
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(result.assignment->slices,
              ElementsAre(SliceIs("b", {0}, false), SliceIs("d", {}, false),
                          SliceIs("", {0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, LongKeysAreDropped) {
  const std::string kLongStartKey(513, 'a');
  const std::string kLongEndKey(513, 'b');
  const std::string kMaxLengthKey(512, 'c');
  AddChunk({{kLongStartKey, std::nullopt, {0}},
            {"", kLongEndKey, {0}},
            {kMaxLengthKey, std::nullopt, {0}}},
           {"host0"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  EXPECT_EQ(result.error_message,
            "1 slice(s) with start_key longer than 512 bytes; "
            "1 slice(s) with end_key longer than 512 bytes; "
            "encountered 1 gap(s) in the assignment");
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(
      result.assignment->slices,
      ElementsAre(SliceIs(kMaxLengthKey, {}, false), SliceIs("", {0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, OverlappingSlicesAreAllDropped) {
  // Both slices of the overlapping pair are dropped.
  AddChunk({{"", "m", {0}}, {"k", "z", {1}}, {"z", std::nullopt, {1}}},
           {"host0", "host1"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  EXPECT_EQ(result.error_message,
            "2 overlapping slices; encountered 1 gap(s) in the assignment");
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(result.assignment->slices,
              ElementsAre(SliceIs("z", {}, false), SliceIs("", {1}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest,
       ChainedOverlappingSlicesAreAllDropped) {
  // The third slice overlaps the second slice, which overlaps the first
  // slice, even though the third slice does not overlap the first slice.
  AddChunk({{"a", "d", {0}},
            {"c", "f", {0}},
            {"e", "h", {0}},
            {"h", std::nullopt, {0}}},
           {"host0"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  EXPECT_EQ(result.error_message,
            "3 overlapping slices; encountered 1 gap(s) in the assignment");
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(result.assignment->slices,
              ElementsAre(SliceIs("h", {}, false), SliceIs("", {0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, ContainedSlicesAreAllDropped) {
  // The first slice contains the next two, which do not overlap each other.
  AddChunk({{"", "z", {0}},
            {"b", "c", {0}},
            {"d", "e", {0}},
            {"z", std::nullopt, {0}}},
           {"host0"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  EXPECT_EQ(result.error_message,
            "3 overlapping slices; encountered 1 gap(s) in the assignment");
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(result.assignment->slices,
              ElementsAre(SliceIs("z", {}, false), SliceIs("", {0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, SlicesWithSameStartKeyOverlap) {
  AddChunk({{"", "m", {0}}, {"", std::nullopt, {0}}}, {"host0"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kRejected);
  EXPECT_EQ(result.error_message,
            "no valid slice assignments in generation 1: 2 overlapping slices");
  EXPECT_FALSE(result.assignment.has_value());
}

TEST_F(AutoshardingAssignmentBuilderTest,
       InfiniteEndKeyBeforeLaterSliceOverlaps) {
  AddChunk({{"", "d", {0}}, {"d", std::nullopt, {0}}, {"m", "z", {0}}},
           {"host0"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  EXPECT_EQ(result.error_message,
            "2 overlapping slices; encountered 1 gap(s) in the assignment");
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(result.assignment->slices,
              ElementsAre(SliceIs("d", {0}, false), SliceIs("", {}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, InvalidSliceDoesNotCauseOverlap) {
  // The second slice has an invalid endpoint index, so it is dropped before
  // the overlap check, and the other slices are kept.
  AddChunk({{"", "m", {0}}, {"k", "z", {1}}, {"m", std::nullopt, {0}}},
           {"host0"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  EXPECT_EQ(result.error_message, "1 slice(s) with invalid endpoint indices");
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(result.assignment->slices,
              ElementsAre(SliceIs("m", {0}, false), SliceIs("", {0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, NoChunksIsRejected) {
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kRejected);
  EXPECT_EQ(result.error_message,
            "received metadata with generation 1, but no chunks were received");
  EXPECT_FALSE(result.assignment.has_value());
}

TEST_F(AutoshardingAssignmentBuilderTest, NoSlicesIsRejected) {
  AddChunk({}, {"host0"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kRejected);
  EXPECT_EQ(result.error_message,
            "no valid slice assignments in generation 1: no slice assignments "
            "received");
  EXPECT_FALSE(result.assignment.has_value());
}

TEST_F(AutoshardingAssignmentBuilderTest, AllSlicesInvalidIsRejected) {
  AddChunk({{"b", "a", {0}}}, {"host0"});
  Result result = Build(1);
  EXPECT_EQ(result.outcome, Outcome::kRejected);
  EXPECT_EQ(result.error_message,
            "no valid slice assignments in generation 1: 1 slice(s) with "
            "start_key >= end_key");
  EXPECT_FALSE(result.assignment.has_value());
}

TEST_F(AutoshardingAssignmentBuilderTest, StaleGeneration) {
  AddChunk({{"", std::nullopt, {0}}}, {"host0"});
  Result result =
      builder_.Build(/*generation=*/5, /*last_accepted_generation=*/5);
  EXPECT_EQ(result.outcome, Outcome::kStale);
  EXPECT_EQ(result.error_message,
            "received generation 5 is not greater than the latest generation "
            "5");
  EXPECT_FALSE(result.assignment.has_value());
  // A newer generation is accepted.
  AddChunk({{"", std::nullopt, {0}}}, {"host0"});
  EXPECT_EQ(
      builder_.Build(/*generation=*/6, /*last_accepted_generation=*/5).outcome,
      Outcome::kAccepted);
}

TEST_F(AutoshardingAssignmentBuilderTest, ZeroGenerationIsStale) {
  AddChunk({{"", std::nullopt, {0}}}, {"host0"});
  Result result =
      builder_.Build(/*generation=*/0, /*last_accepted_generation=*/0);
  EXPECT_EQ(result.outcome, Outcome::kStale);
}

TEST_F(AutoshardingAssignmentBuilderTest, NegativeGenerationIsStale) {
  AddChunk({{"", std::nullopt, {0}}}, {"host0"});
  Result result =
      builder_.Build(/*generation=*/-1, /*last_accepted_generation=*/0);
  EXPECT_EQ(result.outcome, Outcome::kStale);
  EXPECT_EQ(result.error_message,
            "received generation -1 is not greater than the latest generation "
            "0");
}

TEST_F(AutoshardingAssignmentBuilderTest, BuildClearsChunks) {
  AddChunk({{"", std::nullopt, {0}}}, {"host0"});
  EXPECT_EQ(Build(1).outcome, Outcome::kAccepted);
  // A second Build() with no new chunks sees no chunks and does not advance
  // the last accepted generation.
  Result result = Build(2);
  EXPECT_EQ(result.outcome, Outcome::kRejected);
  EXPECT_EQ(result.error_message,
            "received metadata with generation 2, but no chunks were received");
  AddChunk({{"", std::nullopt, {0}}}, {"host0"});
  EXPECT_EQ(Build(2).outcome, Outcome::kAccepted);
}

TEST_F(AutoshardingAssignmentBuilderTest, StaleBuildClearsChunks) {
  AddChunk({{"", std::nullopt, {0}}}, {"host0"});
  EXPECT_EQ(Build(1).outcome, Outcome::kAccepted);
  AddChunk({{"", std::nullopt, {0}}}, {"host0"});
  EXPECT_EQ(Build(1).outcome, Outcome::kStale);
  AddChunk({{"", std::nullopt, {0}}}, {"host1"});
  Result result = Build(2);
  EXPECT_EQ(result.outcome, Outcome::kAccepted);
  ASSERT_TRUE(result.assignment.has_value());
  EXPECT_THAT(result.assignment->endpoint_names, ElementsAre("host1"));
}

TEST_F(AutoshardingAssignmentBuilderTest, BuilderDoesNotTrackGeneration) {
  // The caller owns the last accepted generation.  If the caller does not
  // store an accepted generation, the builder accepts it again.
  AddChunk({{"", std::nullopt, {0}}}, {"host0"});
  EXPECT_EQ(
      builder_.Build(/*generation=*/2, /*last_accepted_generation=*/0).outcome,
      Outcome::kAccepted);
  AddChunk({{"", std::nullopt, {0}}}, {"host0"});
  EXPECT_EQ(
      builder_.Build(/*generation=*/2, /*last_accepted_generation=*/0).outcome,
      Outcome::kAccepted);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
