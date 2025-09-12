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

#include "extract_bazelmod_repositories.h"

#include <absl/strings/match.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"

namespace {

class BazelModParser {
 public:
  absl::Status Parse(std::string_view line) {
    static const std::unordered_set<std::string_view> kIgnoredAttributes = {
        "name",
        "patch_strip",
        "patches",
        "remote_file_integrity",
        "remote_file_urls",
        "remote_patches",
        "remote_patch_strip",
    };
    // Drop comment lines
    if (absl::StartsWith(line, "# ") || line.empty()) {
      return absl::OkStatus();
    }
    // Module name
    std::string_view module_name = line;
    if (absl::ConsumePrefix(&module_name, "## ") &&
        absl::ConsumeSuffix(&module_name, ":")) {
      if (current_.has_value()) {
        return absl::FailedPreconditionError(
            absl::StrFormat("Rule %s started before rule %s was closed",
                            module_name, current_->alias()));
      }
      current_.emplace(module_name);
      return absl::OkStatus();
    }
    if (!current_.has_value()) {
      return absl::FailedPreconditionError(
          absl::StrFormat("Line \"%s\" is outside any rule", line));
    }
    if (line == "http_archive(") {
      return absl::OkStatus();
    }
    if (line == ")") {
      repository_.emplace_back(std::move(current_).value());
      current_.reset();
      return absl::OkStatus();
    }
    std::string_view property_name_value = line;
    if (!absl::ConsumePrefix(&property_name_value, "  ") ||
        !absl::ConsumeSuffix(&property_name_value, ",")) {
      return absl::FailedPreconditionError(absl::StrFormat(
          "Unexpected line \"%s\" in rule %s", line, current_->alias()));
    }
    std::pair<std::string_view, std::string_view> name_value =
        absl::StrSplit(property_name_value, " = ");
    if (name_value.first.empty() || name_value.second.empty()) {
      return absl::FailedPreconditionError(absl::StrFormat(
          "Line \"%s\" in rule %s is not a property", line, current_->alias()));
    }
    if (kIgnoredAttributes.find(name_value.first) != kIgnoredAttributes.end()) {
      // Ignore
    } else if (name_value.first == "integrity") {
      current_->set_integrity(
          absl::StripSuffix(absl::StripPrefix(name_value.second, "\""), "\""));
    } else if (name_value.first == "strip_prefix") {
      current_->set_strip_prefix(
          absl::StripSuffix(absl::StripPrefix(name_value.second, "\""), "\""));
    } else if (name_value.first == "urls") {
      absl::InlinedVector<std::string, 5> urls = absl::StrSplit(
          absl::StripPrefix(absl::StripSuffix(name_value.second, "]"), "["),
          ", ");
      std::transform(
          urls.begin(), urls.end(), urls.begin(), [](std::string_view url) {
            return absl::StripPrefix(absl::StripSuffix(url, "\""), "\"");
          });
      current_->set_urls(urls);
    } else {
      LOG(INFO) << name_value.first << " = " << name_value.second;
    }
    return absl::OkStatus();
  }

  std::vector<BazelModRepository> repository() const { return repository_; }

 private:
  std::optional<BazelModRepository> current_;
  std::vector<BazelModRepository> repository_;
};

}  // namespace

std::string BazelModRepository::Stringify() const {
  return absl::StrFormat(
      "%s = { integrity = \"%s\", strip_prefix = \"%s\", urls = [%s] }", alias_,
      integrity_, strip_prefix_,
      absl::StrJoin(urls_, ", ", [](std::string* dest, const std::string& url) {
        absl::StrAppend(dest, "\"", url, "\"");
      }));
}

// static
absl::StatusOr<std::vector<BazelModRepository>>
BazelModRepository::ParseBazelOutput(const std::string& archives_query_path) {
  std::ifstream reader(archives_query_path);
  if (!reader.is_open()) {
    return absl::UnavailableError(
        absl::Substitute("Can't open $0\n", archives_query_path));
  }
  std::string line;
  BazelModParser parser;
  while (std::getline(reader, line)) {
    if (absl::Status status = parser.Parse(line); !status.ok()) {
      return status;
    }
  }
  return parser.repository();
}