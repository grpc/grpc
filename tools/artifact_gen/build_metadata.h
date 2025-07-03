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