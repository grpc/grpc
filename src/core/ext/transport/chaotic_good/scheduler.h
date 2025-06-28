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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SCHEDULER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SCHEDULER_H

#include <memory>
#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "src/core/ext/transport/chaotic_good/tcp_ztrace_collector.h"

namespace grpc_core::chaotic_good {

// Scheduler defines an interface for scheduling frames across multiple data
// endpoints.
// This class is used in two phases:
// Phase 1: The scheduler collects data to make decisions for a quantum.
// Transition: The scheduler makes a plan for the outstanding work.
// Phase 2: The scheduler allocates messages against its plan.
class Scheduler {
 public:
  virtual ~Scheduler() = default;

  virtual void SetConfig(absl::string_view name, absl::string_view value) = 0;

  // Phase 1: NewStep, then AddChannel repeatedly.
  virtual void NewStep(double outstanding_bytes, double min_tokens) = 0;

  // Channels are re-added every scheduling step.
  // id - indicates a persistent channel id
  // ready - indicates whether the channel is ready to send frames.
  // start_time - if a byte were sent now, how many seconds would it take to
  // be received - includes kernel queue time, rtt, etc.
  // bytes_per_second - the currently observed data rate of the channel.
  virtual void AddChannel(uint32_t id, bool ready, double start_time,
                          double bytes_per_second) = 0;

  // Transition: Make a plan for the outstanding work.
  virtual void MakePlan(TcpZTraceCollector& ztrace_collector) = 0;

  // Phase 2: Allocate messages against the plan.
  // If successful, returns the id of a ready channel to assign the bytes.
  // If this is not possible (all messages must go to non-ready channels),
  // returns nullopt.
  virtual std::optional<uint32_t> AllocateMessage(uint64_t bytes) = 0;

  // Should only return config data.
  virtual std::string Config() const = 0;
};

std::unique_ptr<Scheduler> MakeScheduler(absl::string_view config);

}  // namespace grpc_core::chaotic_good

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SCHEDULER_H
