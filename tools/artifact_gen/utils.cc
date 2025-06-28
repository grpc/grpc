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
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <iostream>

#include "include/yaml-cpp/yaml.h"

namespace {

nlohmann::json YamlToJson(YAML::Node node) {
  switch (node.Type()) {
    case YAML::NodeType::Undefined:
      LOG_FATAL("Undefined YAML node");
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
  LOG_FATAL("Unknown YAML node type: " + std::to_string(node.Type()));
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
      result->push_back(entry.path().lexically_relative(root_dir).string());
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
  ARTIFACT_CHECK(file.is_open(), "Failed to open file: " + filename);
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

// String utilities implementation
namespace strings {
  bool StartsWith(const std::string& str, const std::string& prefix) {
    return str.size() >= prefix.size() && 
           str.compare(0, prefix.size(), prefix) == 0;
  }

  bool EndsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() && 
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
  }

  std::string Replace(std::string str, const std::string& from, const std::string& to) {
    size_t pos = str.find(from);
    if (pos != std::string::npos) {
      str.replace(pos, from.length(), to);
    }
    return str;
  }

  std::string ReplaceAll(std::string str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
      str.replace(pos, from.length(), to);
      pos += to.length();
    }
    return str;
  }

  std::string Join(const std::vector<std::string>& parts, const std::string& separator) {
    if (parts.empty()) return "";
    std::string result = parts[0];
    for (size_t i = 1; i < parts.size(); ++i) {
      result += separator + parts[i];
    }
    return result;
  }
}  // namespace strings

// Logging utilities implementation
namespace logging {
  void LogFatal(const std::string& message, const char* file, int line) {
    std::cerr << "FATAL " << file << ":" << line << ": " << message << std::endl;
    std::exit(1);
  }

  void LogError(const std::string& message) {
    std::cerr << "ERROR: " << message << std::endl;
  }

  void Check(bool condition, const std::string& message, const char* file, int line) {
    if (!condition) {
      LogFatal("Check failed: " + message, file, line);
    }
  }
}  // namespace logging

// Flag parsing implementation
namespace flags {
  static std::vector<Flag> registered_flags;
  static std::map<std::string, bool*> bool_flags;
  static std::map<std::string, bool> bool_defaults;

  void RegisterFlag(const std::string& name, const std::string& description,
                   std::string* value, const std::string& default_value) {
    *value = default_value;
    registered_flags.push_back({name, description, value, default_value});
  }

  void RegisterBoolFlag(const std::string& name, const std::string& description,
                       bool* value, bool default_value) {
    *value = default_value;
    bool_flags[name] = value;
    bool_defaults[name] = default_value;
    registered_flags.push_back({name, description, nullptr, default_value ? "true" : "false"});
  }

  void ParseCommandLine(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--help") {
        PrintHelp();
        std::exit(0);
      }
      if (arg.substr(0, 2) == "--") {
        std::string flag_name = arg.substr(2);
        size_t eq_pos = flag_name.find('=');
        
        std::string name, value;
        if (eq_pos != std::string::npos) {
          name = flag_name.substr(0, eq_pos);
          value = flag_name.substr(eq_pos + 1);
        } else {
          name = flag_name;
          if (i + 1 < argc && argv[i + 1][0] != '-') {
            value = argv[++i];
          } else {
            // Boolean flag without value, set to true
            value = "true";
          }
        }

        // Handle boolean flags
        auto bool_it = bool_flags.find(name);
        if (bool_it != bool_flags.end()) {
          *(bool_it->second) = (value == "true" || value == "1" || value == "yes");
          continue;
        }

        // Handle string flags
        bool found = false;
        for (auto& flag : registered_flags) {
          if (flag.name == name && flag.value != nullptr) {
            *(flag.value) = value;
            found = true;
            break;
          }
        }
        
        if (!found) {
          std::cerr << "Unknown flag: --" << name << std::endl;
          std::exit(1);
        }
      }
    }
  }

  void PrintHelp() {
    std::cout << "Available flags:" << std::endl;
    for (const auto& flag : registered_flags) {
      std::cout << "  --" << flag.name << " (" << flag.description << "); default: \"" 
                << flag.default_value << "\"" << std::endl;
    }
  }
}  // namespace flags
