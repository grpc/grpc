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

#include "src/core/load_balancing/slicer/slice_map.h"

#include <grpc/impl/connectivity_state.h>

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {
namespace {

using ::testing::UnorderedElementsAre;

// EndpointState with the given connectivity state and a null picker (the picker
// is not exercised by these tests).
RefCountedPtr<EndpointState> Endpoint(grpc_connectivity_state state) {
  return MakeRefCounted<EndpointState>(state, nullptr);
}

// Returns the connectivity states of the endpoints in `slice`'s bucket for
// `state`, resolved via `slice_map.all_endpoints()`. Used to check that indices
// land in the right bucket and point at the right endpoints.
std::vector<grpc_connectivity_state> StatesInBucket(
    const SliceMap& slice_map, const SliceEntry& slice,
    grpc_connectivity_state state) {
  std::vector<grpc_connectivity_state> result;
  for (size_t index : slice.endpoints_by_state[state]) {
    result.push_back(slice_map.all_endpoints()[index]->connectivity_state());
  }
  return result;
}

TEST(SliceMapTest, TotalFallbackWhenNoAssignment) {
  SliceMap::EndpointMap endpoints;
  endpoints["a"] = Endpoint(GRPC_CHANNEL_READY);
  endpoints["b"] = Endpoint(GRPC_CHANNEL_CONNECTING);

  auto slice_map = SliceMap::Make(endpoints, /*assignment=*/nullptr);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();

  // No slices: every lookup falls back.
  EXPECT_TRUE((*slice_map)->slices().empty());
  EXPECT_EQ((*slice_map)->Lookup(""), nullptr);
  EXPECT_EQ((*slice_map)->Lookup("anything"), nullptr);
  // The fallback pool is every endpoint.
  EXPECT_EQ((*slice_map)->all_endpoints().size(), 2);
  EXPECT_EQ((*slice_map)->generation(), 0);
}

TEST(SliceMapTest, EmptyAssignmentSlicesIsTotalFallback) {
  SliceMap::EndpointMap endpoints;
  endpoints["a"] = Endpoint(GRPC_CHANNEL_READY);

  SliceMap::LogicalAssignment assignment;
  assignment.generation = 7;  // No slices.

  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();
  EXPECT_TRUE((*slice_map)->slices().empty());
  EXPECT_EQ((*slice_map)->all_endpoints().size(), 1);
  // A total-fallback map has no assignment, so generation is 0.
  EXPECT_EQ((*slice_map)->generation(), 0);
}

TEST(SliceMapTest, LookupFindsContainingSlice) {
  SliceMap::EndpointMap endpoints;
  endpoints["a"] = Endpoint(GRPC_CHANNEL_READY);
  endpoints["b"] = Endpoint(GRPC_CHANNEL_READY);
  endpoints["c"] = Endpoint(GRPC_CHANNEL_READY);

  // Deliberately out of order to exercise the sort in Make().
  SliceMap::LogicalAssignment assignment;
  assignment.generation = 42;
  assignment.slices = {
      {/*start_key=*/"m", {"b"}},
      {/*start_key=*/"a", {"a"}},
      {/*start_key=*/"t", {"c"}},
  };

  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();
  ASSERT_EQ((*slice_map)->slices().size(), 3);

  // Slices are sorted by start_key.
  EXPECT_EQ((*slice_map)->slices()[0].start_key, "a");
  EXPECT_EQ((*slice_map)->slices()[1].start_key, "m");
  EXPECT_EQ((*slice_map)->slices()[2].start_key, "t");

  // Below the first start_key => no slice => fallback.
  EXPECT_EQ((*slice_map)->Lookup("0"), nullptr);
  // Exactly on a start_key => that slice.
  EXPECT_EQ((*slice_map)->Lookup("a"), &(*slice_map)->slices()[0]);
  EXPECT_EQ((*slice_map)->Lookup("m"), &(*slice_map)->slices()[1]);
  // Inside a range => the slice with the greatest start_key <= key.
  EXPECT_EQ((*slice_map)->Lookup("f"), &(*slice_map)->slices()[0]);
  EXPECT_EQ((*slice_map)->Lookup("s"), &(*slice_map)->slices()[1]);
  // Above all start_keys => the last slice.
  EXPECT_EQ((*slice_map)->Lookup("z"), &(*slice_map)->slices()[2]);

  EXPECT_EQ((*slice_map)->generation(), 42);
}

TEST(SliceMapTest, EndpointsBucketedByState) {
  SliceMap::EndpointMap endpoints;
  endpoints["ready1"] = Endpoint(GRPC_CHANNEL_READY);
  endpoints["ready2"] = Endpoint(GRPC_CHANNEL_READY);
  endpoints["connecting"] = Endpoint(GRPC_CHANNEL_CONNECTING);
  endpoints["tf"] = Endpoint(GRPC_CHANNEL_TRANSIENT_FAILURE);

  SliceMap::LogicalAssignment assignment;
  assignment.slices = {
      {/*start_key=*/"", {"ready1", "connecting", "ready2", "tf"}},
  };

  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();
  ASSERT_EQ((*slice_map)->slices().size(), 1);
  const SliceEntry& slice = (*slice_map)->slices()[0];

  EXPECT_THAT(StatesInBucket(**slice_map, slice, GRPC_CHANNEL_READY),
              UnorderedElementsAre(GRPC_CHANNEL_READY, GRPC_CHANNEL_READY));
  EXPECT_THAT(StatesInBucket(**slice_map, slice, GRPC_CHANNEL_CONNECTING),
              UnorderedElementsAre(GRPC_CHANNEL_CONNECTING));
  EXPECT_THAT(StatesInBucket(**slice_map, slice, GRPC_CHANNEL_TRANSIENT_FAILURE),
              UnorderedElementsAre(GRPC_CHANNEL_TRANSIENT_FAILURE));
  EXPECT_TRUE(slice.endpoints_by_state[GRPC_CHANNEL_IDLE].empty());
  EXPECT_FALSE(slice.in_fallback);
}

TEST(SliceMapTest, UnmappedEndpointsAppendedToFallbackPoolOnly) {
  SliceMap::EndpointMap endpoints;
  endpoints["assigned"] = Endpoint(GRPC_CHANNEL_READY);
  endpoints["unmapped1"] = Endpoint(GRPC_CHANNEL_READY);
  endpoints["unmapped2"] = Endpoint(GRPC_CHANNEL_IDLE);

  SliceMap::LogicalAssignment assignment;
  assignment.slices = {{/*start_key=*/"", {"assigned"}}};

  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();

  // All three endpoints are in the fallback pool.
  EXPECT_EQ((*slice_map)->all_endpoints().size(), 3);
  // But the single slice contains only the assigned endpoint.
  ASSERT_EQ((*slice_map)->slices().size(), 1);
  const SliceEntry& slice = (*slice_map)->slices()[0];
  size_t total = 0;
  for (const auto& bucket : slice.endpoints_by_state) total += bucket.size();
  EXPECT_EQ(total, 1);
}

TEST(SliceMapTest, InFallbackWhenSliceHasNoEndpoints) {
  SliceMap::EndpointMap endpoints;
  endpoints["a"] = Endpoint(GRPC_CHANNEL_READY);

  SliceMap::LogicalAssignment assignment;
  assignment.slices = {{/*start_key=*/"", /*endpoint_names=*/{}}};

  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();
  ASSERT_EQ((*slice_map)->slices().size(), 1);
  EXPECT_TRUE((*slice_map)->slices()[0].in_fallback);
}

TEST(SliceMapTest, InFallbackWhenAllEndpointsInTransientFailure) {
  SliceMap::EndpointMap endpoints;
  endpoints["tf1"] = Endpoint(GRPC_CHANNEL_TRANSIENT_FAILURE);
  endpoints["tf2"] = Endpoint(GRPC_CHANNEL_TRANSIENT_FAILURE);

  SliceMap::LogicalAssignment assignment;
  assignment.slices = {{/*start_key=*/"", {"tf1", "tf2"}}};

  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();
  ASSERT_EQ((*slice_map)->slices().size(), 1);
  EXPECT_TRUE((*slice_map)->slices()[0].in_fallback);
}

TEST(SliceMapTest, NotInFallbackWithOneNonTransientFailureEndpoint) {
  SliceMap::EndpointMap endpoints;
  endpoints["tf"] = Endpoint(GRPC_CHANNEL_TRANSIENT_FAILURE);
  endpoints["ready"] = Endpoint(GRPC_CHANNEL_READY);

  SliceMap::LogicalAssignment assignment;
  assignment.slices = {{/*start_key=*/"", {"tf", "ready"}}};

  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();
  ASSERT_EQ((*slice_map)->slices().size(), 1);
  EXPECT_FALSE((*slice_map)->slices()[0].in_fallback);
}

TEST(SliceMapTest, MissingAssignedEndpointIsError) {
  SliceMap::EndpointMap endpoints;
  endpoints["a"] = Endpoint(GRPC_CHANNEL_READY);

  SliceMap::LogicalAssignment assignment;
  assignment.slices = {{/*start_key=*/"", {"a", "does-not-exist"}}};

  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_FALSE(slice_map.ok());
  EXPECT_EQ(slice_map.status().code(), absl::StatusCode::kNotFound);
}

TEST(SliceMapTest, EndpointSharedAcrossSlicesIsDeduplicated) {
  SliceMap::EndpointMap endpoints;
  endpoints["shared"] = Endpoint(GRPC_CHANNEL_READY);
  endpoints["other"] = Endpoint(GRPC_CHANNEL_READY);

  SliceMap::LogicalAssignment assignment;
  assignment.slices = {
      {/*start_key=*/"a", {"shared", "other"}},
      {/*start_key=*/"b", {"shared"}},
  };

  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();
  // "shared" appears in both slices but is stored once in all_endpoints().
  EXPECT_EQ((*slice_map)->all_endpoints().size(), 2);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
