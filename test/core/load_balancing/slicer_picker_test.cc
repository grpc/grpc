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

#include "src/core/load_balancing/slicer/slicer_picker.h"

#include <grpc/impl/connectivity_state.h>

#include <optional>
#include <string>
#include <variant>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/slicer/slice_map.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {
namespace {

using ::testing::AnyOf;

constexpr absl::string_view kHeader = "slice-key";

using PickArgs = LoadBalancingPolicy::PickArgs;
using PickResult = LoadBalancingPolicy::PickResult;
using SubchannelPicker = LoadBalancingPolicy::SubchannelPicker;

// A child picker that identifies itself by failing with its tag as the status
// message. Delegating to an endpoint therefore produces a Fail whose message
// tells us which endpoint's picker was invoked.
class TaggedPicker final : public SubchannelPicker {
 public:
  explicit TaggedPicker(std::string tag) : tag_(std::move(tag)) {}
  PickResult Pick(PickArgs /*args*/) override {
    return PickResult::Fail(absl::UnavailableError(tag_));
  }

 private:
  std::string tag_;
};

// Metadata carrying (at most) a single header value.
class FakeMetadata final : public LoadBalancingPolicy::MetadataInterface {
 public:
  explicit FakeMetadata(std::optional<std::string> value)
      : value_(std::move(value)) {}
  std::optional<absl::string_view> Lookup(absl::string_view key,
                                          std::string* /*buffer*/) const override {
    if (value_.has_value() && key == kHeader) return absl::string_view(*value_);
    return std::nullopt;
  }

 private:
  std::optional<std::string> value_;
};

RefCountedPtr<EndpointState> MakeEndpoint(grpc_connectivity_state state,
                                          std::string tag,
                                          int* exit_idle_count = nullptr) {
  absl::AnyInvocable<void()> on_exit_idle = nullptr;
  if (exit_idle_count != nullptr) {
    on_exit_idle = [exit_idle_count]() { ++*exit_idle_count; };
  }
  return MakeRefCounted<EndpointState>(
      state, MakeRefCounted<TaggedPicker>(std::move(tag)),
      std::move(on_exit_idle));
}

// If the pick delegated to a TaggedPicker (or otherwise failed), returns the
// status message; std::nullopt for a non-Fail result.
std::optional<std::string> FailMessage(const PickResult& result) {
  if (const auto* fail = std::get_if<PickResult::Fail>(&result.result)) {
    return std::string(fail->status.message());
  }
  return std::nullopt;
}

bool IsQueue(const PickResult& result) {
  return std::holds_alternative<PickResult::Queue>(result.result);
}

PickResult DoPick(SlicerPicker& picker, std::optional<std::string> header_value) {
  FakeMetadata metadata(std::move(header_value));
  PickArgs args{/*path=*/"", &metadata, /*call_state=*/nullptr};
  return picker.Pick(args);
}

// Builds a single-slice assignment starting at "" with the given endpoint names.
SliceMap::LogicalAssignment OneSlice(std::vector<std::string> endpoint_names) {
  SliceMap::LogicalAssignment assignment;
  assignment.slices = {{/*start_key=*/"", std::move(endpoint_names)}};
  return assignment;
}

TEST(SlicerPickerTest, NoAssignmentFallbackDisabledFails) {
  SliceMap::EndpointMap endpoints;
  endpoints["a"] = MakeEndpoint(GRPC_CHANNEL_READY, "a");
  auto slice_map = SliceMap::Make(endpoints, /*assignment=*/nullptr);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();

  SlicerPicker picker(*slice_map, std::string(kHeader),
                      /*fallback_enabled=*/false);
  auto result = DoPick(picker, "anything");
  ASSERT_TRUE(FailMessage(result).has_value());
  EXPECT_THAT(*FailMessage(result), ::testing::HasSubstr("no slice assignment"));
}

TEST(SlicerPickerTest, NoAssignmentFallbackEnabledUsesFallbackPool) {
  SliceMap::EndpointMap endpoints;
  endpoints["a"] = MakeEndpoint(GRPC_CHANNEL_READY, "a");
  auto slice_map = SliceMap::Make(endpoints, /*assignment=*/nullptr);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();

  SlicerPicker picker(*slice_map, std::string(kHeader),
                      /*fallback_enabled=*/true);
  // Single endpoint in the fallback pool => deterministic delegation to "a".
  EXPECT_EQ(FailMessage(DoPick(picker, "anything")), "a");
}

TEST(SlicerPickerTest, ReadySliceDelegatesToReadyEndpoint) {
  SliceMap::EndpointMap endpoints;
  endpoints["r"] = MakeEndpoint(GRPC_CHANNEL_READY, "r");
  auto assignment = OneSlice({"r"});
  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();

  SlicerPicker picker(*slice_map, std::string(kHeader),
                      /*fallback_enabled=*/false);
  EXPECT_EQ(FailMessage(DoPick(picker, "k")), "r");
}

TEST(SlicerPickerTest, MissingHeaderUsesEmptyKey) {
  SliceMap::EndpointMap endpoints;
  endpoints["r"] = MakeEndpoint(GRPC_CHANNEL_READY, "r");
  auto assignment = OneSlice({"r"});  // slice starts at "".
  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();

  SlicerPicker picker(*slice_map, std::string(kHeader),
                      /*fallback_enabled=*/false);
  // No header => empty key => matches the slice starting at "".
  EXPECT_EQ(FailMessage(DoPick(picker, std::nullopt)), "r");
}

TEST(SlicerPickerTest, IdleWithNoReadyTriggersExitIdleAndQueues) {
  int exit_idle_count = 0;
  SliceMap::EndpointMap endpoints;
  endpoints["i"] = MakeEndpoint(GRPC_CHANNEL_IDLE, "i", &exit_idle_count);
  auto assignment = OneSlice({"i"});
  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();

  SlicerPicker picker(*slice_map, std::string(kHeader),
                      /*fallback_enabled=*/false);
  EXPECT_TRUE(IsQueue(DoPick(picker, "k")));
  // A second pick must not re-trigger the connection attempt.
  EXPECT_TRUE(IsQueue(DoPick(picker, "k")));
  EXPECT_EQ(exit_idle_count, 1);
}

TEST(SlicerPickerTest, IdleWithReadyDelegatesToReady) {
  int exit_idle_count = 0;
  SliceMap::EndpointMap endpoints;
  endpoints["i"] = MakeEndpoint(GRPC_CHANNEL_IDLE, "i", &exit_idle_count);
  endpoints["r"] = MakeEndpoint(GRPC_CHANNEL_READY, "r");
  auto assignment = OneSlice({"i", "r"});
  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();

  SlicerPicker picker(*slice_map, std::string(kHeader),
                      /*fallback_enabled=*/false);
  // Whether the random endpoint is "i" or "r", the pick resolves to "r": either
  // by direct delegation, or by falling through from IDLE to the READY list.
  EXPECT_EQ(FailMessage(DoPick(picker, "k")), "r");
}

TEST(SlicerPickerTest, ConnectingQueues) {
  SliceMap::EndpointMap endpoints;
  endpoints["c"] = MakeEndpoint(GRPC_CHANNEL_CONNECTING, "c");
  auto assignment = OneSlice({"c"});
  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();

  SlicerPicker picker(*slice_map, std::string(kHeader),
                      /*fallback_enabled=*/false);
  EXPECT_TRUE(IsQueue(DoPick(picker, "k")));
}

TEST(SlicerPickerTest, AllTransientFailureFallbackDisabledFailsViaEndpointPicker) {
  SliceMap::EndpointMap endpoints;
  endpoints["tf1"] = MakeEndpoint(GRPC_CHANNEL_TRANSIENT_FAILURE, "tf1");
  endpoints["tf2"] = MakeEndpoint(GRPC_CHANNEL_TRANSIENT_FAILURE, "tf2");
  auto assignment = OneSlice({"tf1", "tf2"});
  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();

  // Slice is in_fallback (all TF), but fallback is disabled, so the pick is
  // served from the slice and fails via the selected endpoint's picker.
  SlicerPicker picker(*slice_map, std::string(kHeader),
                      /*fallback_enabled=*/false);
  auto msg = FailMessage(DoPick(picker, "k"));
  ASSERT_TRUE(msg.has_value());
  EXPECT_THAT(*msg, AnyOf("tf1", "tf2"));
}

TEST(SlicerPickerTest, EmptySliceQueues) {
  SliceMap::EndpointMap endpoints;
  endpoints["a"] = MakeEndpoint(GRPC_CHANNEL_READY, "a");
  auto assignment = OneSlice({});  // Slice with no endpoints.
  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();

  SlicerPicker picker(*slice_map, std::string(kHeader),
                      /*fallback_enabled=*/false);
  EXPECT_TRUE(IsQueue(DoPick(picker, "k")));
}

TEST(SlicerPickerTest, SliceInFallbackUsesGlobalFallbackPool) {
  // The slice contains only a TRANSIENT_FAILURE endpoint (=> in_fallback), while
  // the resolver also has an unassigned READY endpoint "fb". With fallback
  // enabled, picks must be served from the global pool (which includes "fb"),
  // not from the slice alone.
  SliceMap::EndpointMap endpoints;
  endpoints["tf"] = MakeEndpoint(GRPC_CHANNEL_TRANSIENT_FAILURE, "tf");
  endpoints["fb"] = MakeEndpoint(GRPC_CHANNEL_READY, "fb");
  auto assignment = OneSlice({"tf"});
  auto slice_map = SliceMap::Make(endpoints, &assignment);
  ASSERT_TRUE(slice_map.ok()) << slice_map.status();

  SlicerPicker picker(*slice_map, std::string(kHeader),
                      /*fallback_enabled=*/true);
  // Over many picks the random fallback selection must include "fb", which is
  // not part of the slice -- proving the global pool is used.
  bool saw_fb = false;
  for (int i = 0; i < 200; ++i) {
    auto msg = FailMessage(DoPick(picker, "k"));
    ASSERT_TRUE(msg.has_value());
    EXPECT_THAT(*msg, AnyOf("tf", "fb"));
    if (*msg == "fb") saw_fb = true;
  }
  EXPECT_TRUE(saw_fb);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
