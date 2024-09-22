// Copyright 2024 gRPC authors.
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

#include <fstream>
#include <sstream>

#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "tools/cpp/runfiles/runfiles.h"

#include "include/inja/inja.hpp"
#include "include/nlohmann/json.hpp"
#include "include/yaml-cpp/yaml.h"

using bazel::tools::cpp::runfiles::Runfiles;

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
}

nlohmann::json LoadYaml(const std::string& filename) {
  return YamlToJson(YAML::LoadFile(filename));
}

std::string LoadString(const std::string& filename) {
  std::ifstream file(filename);
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}
}  // namespace

int main(int argc, char** argv) {
  auto positional_args = absl::ParseCommandLine(argc, argv);
  if (positional_args.size() != 2) {
    LOG(FATAL) << "Usage: " << argv[0] << " <input>";
  }
  absl::InitializeLog();
  std::string error;
  std::unique_ptr<Runfiles> runfiles(Runfiles::Create(argv[0], &error));
  if (runfiles == nullptr) {
    LOG(FATAL) << "Failed to load runfiles: " << error;
  }
  LOG(INFO) << "Loading resources";
  auto build_handwritten = LoadYaml(
      runfiles->Rlocation("com_github_grpc_grpc/build_handwritten.yaml"));
  LOG(INFO) << "Loaded build_handwritten.yaml: " << build_handwritten.dump();
  auto template_json = LoadString(positional_args[1]);
  std::cout << inja::render(template_json, build_handwritten);
  return 0;
}
