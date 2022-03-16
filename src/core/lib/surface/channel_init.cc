/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/channel_init.h"

#include <algorithm>

namespace grpc_core {

void ChannelInit::Builder::RegisterStage(grpc_channel_stack_type type,
                                         int priority, Stage stage) {
  slots_[type].emplace_back(std::move(stage), priority);
}

ChannelInit ChannelInit::Builder::Build() {
  ChannelInit result;
  for (int i = 0; i < GRPC_NUM_CHANNEL_STACK_TYPES; i++) {
    auto& slots = slots_[i];
    std::stable_sort(
        slots.begin(), slots.end(),
        [](const Slot& a, const Slot& b) { return a.priority < b.priority; });
    auto& result_slots = result.slots_[i];
    result_slots.reserve(slots.size());
    for (auto& slot : slots) {
      result_slots.emplace_back(std::move(slot.stage));
    }
  }
  return result;
}

bool ChannelInit::CreateStack(ChannelStackBuilder* builder) const {
  for (const auto& stage : slots_[builder->channel_stack_type()]) {
    if (!stage(builder)) return false;
  }
  return true;
}

}  // namespace grpc_core
