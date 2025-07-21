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

#include "build_metadata.h"

#include <fstream>
#include <map>
#include <string>

#include "include/nlohmann/json.hpp"

namespace grpc_tools {
namespace artifact_gen {

nlohmann::json GetBuildExtraMetadata() {
  // Read the JSON file from the runfiles location
  // Bazel makes this available at a predictable path
  std::ifstream file("tools/artifact_gen/build_metadata.json");
  if (!file.is_open()) {
    // Fallback: try relative path for local development
    file.open("build_metadata.json");
    if (!file.is_open()) {
      throw std::runtime_error("Could not open build_metadata.json");
    }
  }
  
  nlohmann::json metadata = nlohmann::json::parse(file, nullptr, true, true);
  return metadata;
}

std::map<std::string, std::string> GetBazelLabelToRenamedMapping() {
  std::map<std::string, std::string> mapping;
  auto metadata = GetBuildExtraMetadata();
  
  for (auto it = metadata.begin(); it != metadata.end(); ++it) {
    const std::string& bazel_label = it.key();
    const auto& lib_metadata = it.value();
    if (lib_metadata.contains("_RENAME")) {
      mapping[bazel_label] = lib_metadata["_RENAME"];
    }
  }
  
  return mapping;
}

}  // namespace artifact_gen
}  // namespace grpc_tools 