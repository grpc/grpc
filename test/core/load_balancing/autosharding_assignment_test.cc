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
using Result = AutoshardingAssignmentBuilder::Result;

auto SliceIs(absl::string_view end_key, std::vector<size_t> endpoints,
             bool fallback_disabled) {
  return FieldsAre(end_key, std::move(endpoints), fallback_disabled);
}

class AutoshardingAssignmentBuilderTest : public ::testing::Test {
 protected:
  struct BuildResult {
    RefCountedPtr<const AutoshardingAssignment> assignment;
    std::string issues;
  };

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

  // Sends an AssignmentChunk, given in textproto form.
  void AddChunk(absl::string_view chunk_textproto) {
    EXPECT_FALSE(ProcessResponse(absl::StrCat("chunk {", chunk_textproto, "}"))
                     .has_value());
  }

  // Sends AssignmentMetadata, and returns the accepted assignment or the
  // reason for rejecting it.
  absl::StatusOr<BuildResult> Build(int64_t generation) {
    std::optional<Result> result = ProcessResponse(
        absl::StrCat("metadata { generation: ", generation, " }"));
    if (!result.has_value()) return absl::InternalError("no result");
    EXPECT_EQ(result->generation, generation);
    if (!result->assignment.ok()) return result->assignment.status();
    return BuildResult{*std::move(result->assignment),
                       std::move(result->issues)};
  }

  upb::Arena arena_;
  AutoshardingAssignmentBuilder builder_;
};

TEST_F(AutoshardingAssignmentBuilderTest, SingleSliceCoversKeyspace) {
  AddChunk(R"pb(
    slice_assignments {
      slice { start_key: "" }
      endpoints { endpoint_index: 0 }
      endpoints { endpoint_index: 1 }
    }
    endpoints { endpoint: "host0" }
    endpoints { endpoint: "host1" }
  )pb");
  absl::StatusOr<BuildResult> result = Build(/*generation=*/1);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->issues, "");
  EXPECT_THAT(result->assignment->endpoint_names,
              ElementsAre("host0", "host1"));
  EXPECT_THAT(result->assignment->slices,
              ElementsAre(SliceIs("", {0, 1}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, MissingSliceCoversKeyspace) {
  AddChunk(R"pb(
    slice_assignments { endpoints { endpoint_index: 0 } }
    endpoints { endpoint: "host0" }
  )pb");
  absl::StatusOr<BuildResult> result = Build(1);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->issues, "");
  EXPECT_THAT(result->assignment->slices, ElementsAre(SliceIs("", {0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, SlicesAreSorted) {
  AddChunk(R"pb(
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
  )pb");
  absl::StatusOr<BuildResult> result = Build(1);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->issues, "");
  EXPECT_THAT(result->assignment->slices,
              ElementsAre(SliceIs("m", {0}, false), SliceIs("", {1}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest,
       EndpointNamesConcatenatedInChunkOrder) {
  // Slices refer to endpoints in later chunks, and slices are spread across
  // chunks.
  AddChunk(R"pb(
    slice_assignments {
      slice { start_key: "" end_key: "m" }
      endpoints { endpoint_index: 2 }
    }
    endpoints { endpoint: "host0" }
  )pb");
  AddChunk(R"pb(
    slice_assignments {
      slice { start_key: "m" }
      endpoints { endpoint_index: 0 }
    }
    endpoints { endpoint: "host1" }
    endpoints { endpoint: "host2" }
  )pb");
  absl::StatusOr<BuildResult> result = Build(1);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(result->assignment->endpoint_names,
              ElementsAre("host0", "host1", "host2"));
  EXPECT_THAT(result->assignment->slices,
              ElementsAre(SliceIs("m", {2}, false), SliceIs("", {0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, DuplicateEndpointIndicesAreKept) {
  AddChunk(R"pb(
    slice_assignments {
      slice { start_key: "" }
      endpoints { endpoint_index: 0 }
      endpoints { endpoint_index: 0 }
    }
    endpoints { endpoint: "host0" }
  )pb");
  absl::StatusOr<BuildResult> result = Build(1);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(result->assignment->slices,
              ElementsAre(SliceIs("", {0, 0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, EmptyEndpointListDisablesFallback) {
  AddChunk(R"pb(
    slice_assignments { slice { start_key: "" end_key: "a" } }
    slice_assignments {
      slice { start_key: "a" end_key: "b" }
      endpoints { endpoint_index: 0 }
    }
    slice_assignments { slice { start_key: "b" } }
    endpoints { endpoint: "host0" }
  )pb");
  absl::StatusOr<BuildResult> result = Build(1);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->issues, "");
  EXPECT_THAT(result->assignment->slices,
              ElementsAre(SliceIs("a", {}, true), SliceIs("b", {0}, false),
                          SliceIs("", {}, true)));
}

TEST_F(AutoshardingAssignmentBuilderTest,
       GapNextToSliceWithoutEndpointsKeepsFallbackEnabled) {
  AddChunk(R"pb(
    slice_assignments { slice { start_key: "b" end_key: "c" } }
    endpoints { endpoint: "host0" }
  )pb");
  absl::StatusOr<BuildResult> result = Build(1);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->issues, "encountered 2 gap(s) in the assignment");
  EXPECT_THAT(result->assignment->slices,
              ElementsAre(SliceIs("b", {}, false), SliceIs("c", {}, true),
                          SliceIs("", {}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, GapsAreFilled) {
  // Gaps at the start, in the middle, and at the end of the keyspace.
  AddChunk(R"pb(
    slice_assignments {
      slice { start_key: "b" end_key: "c" }
      endpoints { endpoint_index: 0 }
    }
    slice_assignments {
      slice { start_key: "d" end_key: "e" }
      endpoints { endpoint_index: 0 }
    }
    endpoints { endpoint: "host0" }
  )pb");
  absl::StatusOr<BuildResult> result = Build(1);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->issues, "encountered 3 gap(s) in the assignment");
  EXPECT_THAT(result->assignment->slices,
              ElementsAre(SliceIs("b", {}, false), SliceIs("c", {0}, false),
                          SliceIs("d", {}, false), SliceIs("e", {0}, false),
                          SliceIs("", {}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, InvalidSlicesBecomeGaps) {
  AddChunk(R"pb(
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
  )pb");
  absl::StatusOr<BuildResult> result = Build(1);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->issues,
            "2 slice(s) with start_key >= end_key; "
            "2 slice(s) with invalid endpoint indices; "
            "encountered 1 gap(s) in the assignment");
  EXPECT_THAT(result->assignment->slices,
              ElementsAre(SliceIs("b", {0}, false), SliceIs("d", {}, false),
                          SliceIs("", {0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, LongKeysAreDropped) {
  const std::string kLongStartKey(513, 'a');
  const std::string kLongEndKey(513, 'b');
  const std::string kMaxLengthKey(512, 'c');
  AddChunk(
      absl::StrCat("slice_assignments { slice { start_key: \"", kLongStartKey,
                   "\" } ", "endpoints { endpoint_index: 0 } } ",
                   "slice_assignments { slice { start_key: \"\" end_key: \"",
                   kLongEndKey, "\" } endpoints { endpoint_index: 0 } } ",
                   "slice_assignments { slice { start_key: \"", kMaxLengthKey,
                   "\" } ", "endpoints { endpoint_index: 0 } } ",
                   "endpoints { endpoint: \"host0\" }"));
  absl::StatusOr<BuildResult> result = Build(1);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->issues,
            "1 slice(s) with start_key longer than 512 bytes; "
            "1 slice(s) with end_key longer than 512 bytes; "
            "encountered 1 gap(s) in the assignment");
  EXPECT_THAT(
      result->assignment->slices,
      ElementsAre(SliceIs(kMaxLengthKey, {}, false), SliceIs("", {0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, OverlappingSlicesAreAllDropped) {
  AddChunk(R"pb(
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
  )pb");
  absl::StatusOr<BuildResult> result = Build(1);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->issues,
            "2 overlapping slices; encountered 1 gap(s) in the assignment");
  EXPECT_THAT(result->assignment->slices,
              ElementsAre(SliceIs("z", {}, false), SliceIs("", {1}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest,
       ChainedOverlappingSlicesAreAllDropped) {
  // The third slice overlaps the second slice, which overlaps the first
  // slice, even though the third slice does not overlap the first slice.
  AddChunk(R"pb(
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
  )pb");
  absl::StatusOr<BuildResult> result = Build(1);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->issues,
            "3 overlapping slices; encountered 1 gap(s) in the assignment");
  EXPECT_THAT(result->assignment->slices,
              ElementsAre(SliceIs("h", {}, false), SliceIs("", {0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, ContainedSlicesAreAllDropped) {
  // The first slice contains the next two, which do not overlap each other.
  AddChunk(R"pb(
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
  )pb");
  absl::StatusOr<BuildResult> result = Build(1);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->issues,
            "3 overlapping slices; encountered 1 gap(s) in the assignment");
  EXPECT_THAT(result->assignment->slices,
              ElementsAre(SliceIs("z", {}, false), SliceIs("", {0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, SlicesWithSameStartKeyOverlap) {
  AddChunk(R"pb(
    slice_assignments {
      slice { start_key: "" end_key: "m" }
      endpoints { endpoint_index: 0 }
    }
    slice_assignments {
      slice { start_key: "" }
      endpoints { endpoint_index: 0 }
    }
    endpoints { endpoint: "host0" }
  )pb");
  EXPECT_EQ(Build(1).status(),
            absl::InvalidArgumentError(
                "no valid slice assignments in generation 1: 2 overlapping "
                "slices"));
}

TEST_F(AutoshardingAssignmentBuilderTest,
       InfiniteEndKeyBeforeLaterSliceOverlaps) {
  AddChunk(R"pb(
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
  )pb");
  absl::StatusOr<BuildResult> result = Build(1);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->issues,
            "2 overlapping slices; encountered 1 gap(s) in the assignment");
  EXPECT_THAT(result->assignment->slices,
              ElementsAre(SliceIs("d", {0}, false), SliceIs("", {}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, InvalidSliceDoesNotCauseOverlap) {
  // The second slice has an invalid endpoint index, so it is dropped before
  // the overlap check, and the other slices are kept.
  AddChunk(R"pb(
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
  )pb");
  absl::StatusOr<BuildResult> result = Build(1);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->issues, "1 slice(s) with invalid endpoint indices");
  EXPECT_THAT(result->assignment->slices,
              ElementsAre(SliceIs("m", {0}, false), SliceIs("", {0}, false)));
}

TEST_F(AutoshardingAssignmentBuilderTest, NoChunksIsRejected) {
  EXPECT_EQ(Build(1).status(),
            absl::InvalidArgumentError(
                "received metadata with generation 1, but no chunks were "
                "received"));
}

TEST_F(AutoshardingAssignmentBuilderTest, NoSlicesIsRejected) {
  AddChunk(R"pb(endpoints { endpoint: "host0" })pb");
  EXPECT_EQ(Build(1).status(),
            absl::InvalidArgumentError(
                "no valid slice assignments in generation 1: no slice "
                "assignments received"));
}

TEST_F(AutoshardingAssignmentBuilderTest, AllSlicesInvalidIsRejected) {
  AddChunk(R"pb(
    slice_assignments {
      slice { start_key: "b" end_key: "a" }
      endpoints { endpoint_index: 0 }
    }
    endpoints { endpoint: "host0" }
  )pb");
  EXPECT_EQ(Build(1).status(),
            absl::InvalidArgumentError(
                "no valid slice assignments in generation 1: 1 slice(s) with "
                "start_key >= end_key"));
}

constexpr absl::string_view kSingleSliceChunk = R"pb(
  slice_assignments {
    slice { start_key: "" }
    endpoints { endpoint_index: 0 }
  }
  endpoints { endpoint: "host0" }
)pb";

TEST_F(AutoshardingAssignmentBuilderTest, StaleGeneration) {
  AddChunk(kSingleSliceChunk);
  ASSERT_TRUE(Build(/*generation=*/5).ok());
  AddChunk(kSingleSliceChunk);
  EXPECT_EQ(Build(5).status(),
            absl::InvalidArgumentError(
                "received generation 5 is not greater than the latest "
                "generation 5"));
  AddChunk(kSingleSliceChunk);
  EXPECT_TRUE(Build(6).ok());
}

TEST_F(AutoshardingAssignmentBuilderTest, ZeroGenerationIsStale) {
  AddChunk(kSingleSliceChunk);
  EXPECT_EQ(Build(0).status(),
            absl::InvalidArgumentError(
                "received generation 0 is not greater than the latest "
                "generation 0"));
}

TEST_F(AutoshardingAssignmentBuilderTest, NegativeGenerationIsStale) {
  AddChunk(kSingleSliceChunk);
  EXPECT_EQ(Build(-1).status(),
            absl::InvalidArgumentError(
                "received generation -1 is not greater than the latest "
                "generation 0"));
}

TEST_F(AutoshardingAssignmentBuilderTest, TracksLastAcceptedGeneration) {
  EXPECT_EQ(builder_.last_accepted_generation(), 0);
  AddChunk(kSingleSliceChunk);
  ASSERT_TRUE(Build(2).ok());
  EXPECT_EQ(builder_.last_accepted_generation(), 2);
  // A rejected assignment does not change the latest accepted generation.
  EXPECT_FALSE(Build(3).ok());
  EXPECT_EQ(builder_.last_accepted_generation(), 2);
}

TEST_F(AutoshardingAssignmentBuilderTest, ResetPartialAssignmentDropsChunks) {
  AddChunk(kSingleSliceChunk);
  builder_.ResetPartialAssignment();
  EXPECT_EQ(Build(1).status(),
            absl::InvalidArgumentError(
                "received metadata with generation 1, but no chunks were "
                "received"));
}

TEST_F(AutoshardingAssignmentBuilderTest,
       ResponsesWithoutAssignmentAreIgnored) {
  EXPECT_FALSE(ProcessResponse("").has_value());
  EXPECT_FALSE(ProcessResponse("config {}").has_value());
}

TEST_F(AutoshardingAssignmentBuilderTest, BuildClearsChunks) {
  AddChunk(kSingleSliceChunk);
  EXPECT_TRUE(Build(1).ok());
  EXPECT_EQ(Build(2).status(),
            absl::InvalidArgumentError(
                "received metadata with generation 2, but no chunks were "
                "received"));
  AddChunk(kSingleSliceChunk);
  EXPECT_TRUE(Build(2).ok());
}

TEST_F(AutoshardingAssignmentBuilderTest, StaleBuildClearsChunks) {
  AddChunk(kSingleSliceChunk);
  EXPECT_TRUE(Build(1).ok());
  AddChunk(kSingleSliceChunk);
  EXPECT_FALSE(Build(1).ok());
  AddChunk(R"pb(
    slice_assignments {
      slice { start_key: "" }
      endpoints { endpoint_index: 0 }
    }
    endpoints { endpoint: "host1" }
  )pb");
  absl::StatusOr<BuildResult> result = Build(2);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(result->assignment->endpoint_names, ElementsAre("host1"));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
