// Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/validation_errors.h"

#include <algorithm>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/strip.h"

namespace grpc_core {

void ValidationErrors::PushField(absl::string_view ext) {
  // Skip leading '.' for top-level field names.
  if (fields_.empty()) absl::ConsumePrefix(&ext, ".");
  fields_.emplace_back(std::string(ext));
}

void ValidationErrors::PopField() { fields_.pop_back(); }

void ValidationErrors::AddError(absl::string_view error) {
  field_errors_[absl::StrJoin(fields_, "")].emplace_back(error);
}

bool ValidationErrors::FieldHasErrors() const {
  return field_errors_.find(absl::StrJoin(fields_, "")) != field_errors_.end();
}

absl::Status ValidationErrors::status(absl::string_view prefix) const {
  if (field_errors_.empty()) return absl::OkStatus();
  std::vector<std::string> errors;
  for (const auto& p : field_errors_) {
    if (p.second.size() > 1) {
      errors.emplace_back(absl::StrCat("field:", p.first, " errors:[",
                                       absl::StrJoin(p.second, "; "), "]"));
    } else {
      errors.emplace_back(
          absl::StrCat("field:", p.first, " error:", p.second[0]));
    }
  }
  return absl::InvalidArgumentError(
      absl::StrCat(prefix, ": [", absl::StrJoin(errors, "; "), "]"));
}

}  // namespace grpc_core
