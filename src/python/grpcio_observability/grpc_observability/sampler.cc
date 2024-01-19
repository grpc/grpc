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
  // We can't directly return probability * UINT64_MAX.
  //
  // UINT64_MAX is (2^64)-1, but as a double rounds up to 2^64.
  // For probabilities >= 1-(2^-54), the product wraps to zero!
  // Instead, calculate the high and low 32 bits separately.
  const double product = UINT32_MAX * probability;
  double hi_bits, lo_bits = ldexp(modf(product, &hi_bits), 32) + product;
  return (static_cast<uint64_t>(hi_bits) << 32) +
         static_cast<uint64_t>(lo_bits);
}

uint64_t CalculateThresholdFromBuffer(const std::string& trace_id) {
  const std::string trace_id_bytes = absl::HexStringToBytes(trace_id);
  const uint8_t* buf = reinterpret_cast<const uint8_t*>(trace_id_bytes.c_str());
  uint64_t res = 0;
  for (int i = 0; i < 8; ++i) {
    res |= (static_cast<uint64_t>(buf[i]) << (i * 8));
  }
  return res;
}
}  // namespace

ProbabilitySampler& ProbabilitySampler::Get() {
  static ProbabilitySampler* sampler = new ProbabilitySampler;
  return *sampler;
}

void ProbabilitySampler::SetThreshold(double probability) {
  uint64_t threshold = CalculateThreshold(probability);
  threshold_ = threshold;
}

bool ProbabilitySampler::ShouldSample(const std::string& trace_id) {
  if (threshold_ == 0 || trace_id.length() < 32) return false;
  // All Spans within the same Trace will get the same sampling decision, so
  // full trees of Spans will be sampled.
  return CalculateThresholdFromBuffer(trace_id) <= threshold_;
}

}  // namespace grpc_observability
