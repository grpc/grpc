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

#include "utils.h"

#include "absl/log/log.h"
#include "include/yaml-cpp/yaml.h"

namespace {

nlohmann::json YamlToJson(YAML::Node node) {
  switch (node.Type()) {
    case YAML::NodeType::Undefined:
      LOG(FATAL) << "Undefined YAML node";
    case YAML::NodeType::Null:
      return nullptr;
    case YAML::NodeType::Scalar:
      return node.Scalar();
    case YAML::NodeType::Sequence: {
      nlohmann::json result = nlohmann::json::array();
      for (const auto& element : node) {
        result.push_back(YamlToJson(element));
      }
      return result;
    }
    case YAML::NodeType::Map: {
      nlohmann::json result = nlohmann::json::object();
      for (const auto& element : node) {
        result[element.first.as<std::string>()] = YamlToJson(element.second);
      }
      return result;
    }
  }
  LOG(FATAL) << "Unknown YAML node type: " << node.Type();
}

}  // namespace

nlohmann::json LoadYaml(const std::string& filename) {
  return YamlToJson(YAML::LoadFile(filename));
}
