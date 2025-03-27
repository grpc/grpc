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

#include <fstream>

#include "absl/log/log.h"
#include "absl/log/check.h"
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

namespace {
void AddAllFilesInDir(const std::string& root_dir, const std::string& dir,
                      std::vector<std::string>* result) {
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (std::filesystem::is_regular_file(entry)) {
      result->push_back(entry.path().lexically_relative(root_dir));
    } else if (std::filesystem::is_directory(entry)) {
      AddAllFilesInDir(root_dir, entry.path().string(), result);
    }
  }
}
}  // namespace

std::vector<std::string> AllFilesInDir(const std::string& dir) {
  std::vector<std::string> result;
  AddAllFilesInDir(dir, dir, &result);
  return result;
}

std::string LoadString(const std::string& filename) {
  std::ifstream file(filename);
  CHECK(file.is_open()) << "Failed to open file: " << filename;
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}
