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

#include "expand_version.h"

#include <optional>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "nlohmann/json.hpp"

void ExpandVersion(nlohmann::json& config) {
  auto& settings = config["settings"];
  std::string version_string = settings["version"];
  std::optional<std::string> tag;
  if (version_string.find("-") != std::string::npos) {
    tag = version_string.substr(version_string.find("-") + 1);
    version_string = version_string.substr(0, version_string.find("-"));
  }
  std::vector<std::string> version_parts = absl::StrSplit(version_string, '.');
  CHECK_EQ(version_parts.size(), 3u);
  int major, minor, patch;
  CHECK(absl::SimpleAtoi(version_parts[0], &major));
  CHECK(absl::SimpleAtoi(version_parts[1], &minor));
  CHECK(absl::SimpleAtoi(version_parts[2], &patch));
  settings["version"] = nlohmann::json::object();
  settings["version"]["string"] = version_string;
  settings["version"]["major"] = major;
  settings["version"]["minor"] = minor;
  settings["version"]["patch"] = patch;
  if (tag) {
    settings["version"]["tag"] = *tag;
  }
  std::string php_version = absl::StrCat(major, ".", minor, ".", patch);
  if (tag.has_value()) {
    if (tag == "dev") {
      php_version += "dev";
    } else if (tag->size() >= 3 && tag->substr(0, 3) == "pre") {
      php_version += "RC" + tag->substr(3);
    } else {
      LOG(FATAL) << "Unknown tag: " << *tag;
    }
  }
  settings["version"]["php"] = php_version;
}
