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

#ifndef GRPC_TOOLS_ARTIFACT_GEN_BUILD_METADATA_H
#define GRPC_TOOLS_ARTIFACT_GEN_BUILD_METADATA_H

#include <map>
#include <string>

#include "include/nlohmann/json.hpp"

namespace grpc_tools {
namespace artifact_gen {

// Returns the complete build extra metadata as parsed JSON
nlohmann::json GetBuildExtraMetadata();

// Returns a mapping from original Bazel labels to renamed library names
// Only includes entries that have a "_RENAME" field
std::map<std::string, std::string> GetBazelLabelToRenamedMapping();

}  // namespace artifact_gen
}  // namespace grpc_tools

#endif  // GRPC_TOOLS_ARTIFACT_GEN_BUILD_METADATA_H 