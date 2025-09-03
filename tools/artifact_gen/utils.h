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

#ifndef GRPC_TOOLS_ARTIFACT_GEN_UTILS_H
#define GRPC_TOOLS_ARTIFACT_GEN_UTILS_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <map>

#include "include/nlohmann/json.hpp"

// Existing utilities
nlohmann::json LoadYaml(const std::string& filename);
std::vector<std::string> AllFilesInDir(const std::string& dir);
std::string LoadString(const std::string& filename);

// String utilities
namespace strings {
  bool StartsWith(const std::string& str, const std::string& prefix);
  bool EndsWith(const std::string& str, const std::string& suffix);
  std::string Replace(std::string str, const std::string& from, const std::string& to);
  std::string ReplaceAll(std::string str, const std::string& from, const std::string& to);
  std::string Join(const std::vector<std::string>& parts, const std::string& separator);
  
  // Variadic StrCat - simple version for common cases
  template<typename... Args>
  std::string StrCat(Args&&... args) {
    std::ostringstream oss;
    (oss << ... << args);
    return oss.str();
  }
}  // namespace strings

// Logging utilities
namespace logging {
  [[noreturn]] void LogFatal(const std::string& message, const char* file, int line);
  void LogError(const std::string& message);
  void Check(bool condition, const std::string& message, const char* file, int line);
}

#define LOG_FATAL(msg) logging::LogFatal(msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) logging::LogError(msg)
#define ARTIFACT_CHECK(cond, msg) logging::Check(cond, msg, __FILE__, __LINE__)

// Simple flag parsing
namespace flags {
  struct Flag {
    std::string name;
    std::string description;
    std::string* value;
    std::string default_value;
  };
  
  void RegisterFlag(const std::string& name, const std::string& description, 
                   std::string* value, const std::string& default_value);
  void ParseCommandLine(int argc, char** argv);
  void PrintHelp();
  
  // Helper for boolean flags
  void RegisterBoolFlag(const std::string& name, const std::string& description,
                       bool* value, bool default_value);
}  // namespace flags

#endif  // GRPC_TOOLS_ARTIFACT_GEN_UTILS_H
