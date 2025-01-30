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

#include "src/core/call/delay_tracker.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace grpc_core {

DelayTracker::Handle DelayTracker::StartDelay(absl::string_view description) {
  Handle handle = delays_.size();
  delays_.emplace_back(description);
  return handle;
}

void DelayTracker::EndDelay(Handle handle) {
  CHECK_LT(handle, delays_.size());
  delays_[handle].end = Timestamp::Now();
}

void DelayTracker::AddChild(absl::string_view description,
                            DelayTracker delay_tracker) {
  children_.emplace_back(description, std::move(delay_tracker));
}

std::string DelayTracker::GetDelayInfo() const {
  std::vector<std::string> parts;
  for (const Delay& delay : delays_) {
    if (delay.end == Timestamp::InfPast()) {
      Duration duration = Timestamp::Now() - delay.start;
      parts.push_back(absl::StrCat(delay.description, " timed out after ",
                                   duration.ToString()));
    } else {
      Duration duration = delay.end - delay.start;
      parts.push_back(
          absl::StrCat(delay.description, " delay ", duration.ToString()));
    }
  }
  for (const Child& child : children_) {
    parts.push_back(absl::StrCat(child.description, ":[",
                                 child.delay_tracker->GetDelayInfo(), "]"));
  }
  return absl::StrJoin(parts, "; ");
}

}  // namespace grpc_core
