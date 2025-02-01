// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CALL_DELAY_TRACKER_H
#define GRPC_SRC_CORE_CALL_DELAY_TRACKER_H

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/time.h"

namespace grpc_core {

// Tracks delays seen while processing a client call.  This information
// is added to the status message when a call fails with DEADLINE_EXCEEDED.
class DelayTracker {
 public:
  using Handle = size_t;

  // Starts recording a delay.  Returns a handle for the new delay.  The
  // caller needs to hold on to the handle and later pass it to EndDelay()
  // when the delay is complete.
  Handle StartDelay(absl::string_view description);

  // Ends a delay.
  void EndDelay(Handle handle);

  // Adds a child DelayTracker.  Used to compose DelayTrackers from
  // multiple parties as server trailing metadata is returned up the stack.
  void AddChild(std::string description, DelayTracker tracker);

  // Reports delay info in a form suitable for inclusion in a status message.
  std::string GetDelayInfo() const;

 private:
  struct Delay {
    std::string description;
    Timestamp start = Timestamp::Now();
    Timestamp end = Timestamp::InfPast();

    explicit Delay(absl::string_view desc) : description(desc) {}
  };

  struct Child {
    std::string description;
    std::unique_ptr<DelayTracker> delay_tracker;

    Child(absl::string_view desc, DelayTracker tracker)
        : description(desc),
          delay_tracker(std::make_unique<DelayTracker>(std::move(tracker))) {}

    // Copyable.
    Child(const Child& other)
        : description(other.description),
          delay_tracker(std::make_unique<DelayTracker>(*other.delay_tracker)) {}
    Child& operator=(const Child& other) {
      description = other.description;
      delay_tracker = std::make_unique<DelayTracker>(*other.delay_tracker);
      return *this;
    }
  };

  std::vector<Delay> delays_;
  std::vector<Child> children_;
};

// Allow DelayTracker to be used as an arena context element.
template <>
struct ArenaContextType<DelayTracker> {
  static void Destroy(DelayTracker*) {}
};

// Gets the DelayTracker from call context, creating it if needed.
inline DelayTracker* GetOrCreateDelayTrackerContext() {
  DelayTracker* tracker = MaybeGetContext<DelayTracker>();
  if (tracker == nullptr) {
    Arena* arena = GetContext<Arena>();
    // TODO(roth): When the legacy filter stack goes away, change this
    // to use New() instead of ManagedNew(), and change the
    // ArenaContextType above to do the deletion instead.
    tracker = arena->ManagedNew<DelayTracker>();
    arena->SetContext<DelayTracker>(tracker);
  }
  return tracker;
}

// Wraps a promise, adding delay tracking if the promise returns Pending{}.
template <typename Promise>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline auto TrackDelay(
    std::string delay_description, Promise promise) {
  using P = promise_detail::PromiseLike<Promise>;
  return
      [delay_description = std::move(delay_description),
       handle = std::optional<DelayTracker::Handle>(),
       promise = P(std::move(promise))]() mutable -> Poll<typename P::Result> {
        auto r = promise();
        if (r.pending()) {
          // If we haven't already started recording a delay, do so now.
          if (!handle.has_value()) {
            DelayTracker* tracker = GetOrCreateDelayTrackerContext();
            handle = tracker->StartDelay(std::move(delay_description));
          }
          return Pending{};
        }
        // If there was a delay, record that the delay is over.
        if (handle.has_value()) {
          DelayTracker* tracker = GetContext<DelayTracker>();
          tracker->EndDelay(*handle);
        }
        return std::move(r.value());
      };
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CALL_DELAY_TRACKER_H
