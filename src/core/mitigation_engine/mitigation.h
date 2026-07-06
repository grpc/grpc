//
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
//

#ifndef GRPC_SRC_CORE_MITIGATION_ENGINE_MITIGATION_H
#define GRPC_SRC_CORE_MITIGATION_ENGINE_MITIGATION_H

#include <optional>

#include "src/core/call/metadata_batch.h"
#include "src/core/mitigation_engine/mitigation_engine.h"
#include "src/core/util/ref_counted.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Mitigations represent read-only configuration rules.
class Mitigation : public RefCounted<Mitigation> {
 public:
  ~Mitigation() override = default;

  virtual std::optional<MitigationEngine::Action>
  EvaluateIncomingMetadataParsed(absl::string_view key,
                                 absl::string_view value) const = 0;
  virtual std::optional<MitigationEngine::Action>
  EvaluateIncomingMetadataAllParsed(
      const grpc_metadata_batch& metadata) const = 0;

  Mitigation() = default;
  // Not movable or copyable.
  Mitigation(Mitigation&&) = delete;
  Mitigation& operator=(Mitigation&&) = delete;
  Mitigation(const Mitigation&) = delete;
  Mitigation& operator=(const Mitigation&) = delete;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_MITIGATION_ENGINE_MITIGATION_H
