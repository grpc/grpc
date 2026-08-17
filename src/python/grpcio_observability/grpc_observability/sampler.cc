// Copyright 2023 gRPC authors.
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

#include "sampler.h"

#include <cmath>
#include <cstdint>

#include "absl/strings/escaping.h"

namespace grpc_observability {

namespace {

// Converts a probability in [0, 1] to a threshold in [0, UINT64_MAX].
uint64_t CalculateThreshold(double probability) {
  if (probability <= 0.0) return 0;
  if (probability >= 1.0) return UINT64_MAX;

  // for the probabilities in range (0.0, 1.0) the result is at most 2^64 - 2^11
  // which fits uint64_t type. Note: the largest double below 1.0 is 1 - 2^(-53)
  return static_cast<uint64_t>(nearbyint(ldexp(probability, 64)));
}

// Reads the lowest 8 bytes of the decoded trace ID as a big-endian integer,
// matching OpenTelemtry SDK's implementation, where the trace ID is parsed
// from its big endian hex representation.
uint64_t CalculateThresholdFromBuffer(const std::string& trace_id) {
  const std::string trace_id_bytes = absl::HexStringToBytes(trace_id);
  if (trace_id_bytes.size() < 16U) return UINT64_MAX;
  const uint8_t* buf = reinterpret_cast<const uint8_t*>(trace_id_bytes.c_str());
  uint64_t res = 0;
  for (int i = 8; i < 16; ++i) {
    res = (res << 8) | (static_cast<uint64_t>(buf[i]));
  }
  return res;
}
}  // namespace

ProbabilitySampler& ProbabilitySampler::Get() {
  static ProbabilitySampler* sampler = new ProbabilitySampler;
  return *sampler;
}

void ProbabilitySampler::SetThreshold(double probability) {
  threshold_.store(CalculateThreshold(probability), std::memory_order_relaxed);
}

bool ProbabilitySampler::ShouldSample(const std::string& trace_id) {
  const uint64_t threshold = threshold_.load(std::memory_order_relaxed);
  if (threshold == 0 || trace_id.length() < 32) return false;
  if (threshold == UINT64_MAX) return true;
  // All Spans within the same Trace will get the same sampling decision, so
  // full trees of Spans will be sampled.
  return CalculateThresholdFromBuffer(trace_id) < threshold;
}

}  // namespace grpc_observability
