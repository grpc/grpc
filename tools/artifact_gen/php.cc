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

#include "php.h"

#include <set>
#include <vector>

#include "absl/log/log.h"

void AddPhpConfig(nlohmann::json& config) {
  std::set<std::string> srcs;
  for (const auto& src : config["php_config_m4"]["src"]) {
    srcs.insert(src);
  }
  std::map<std::string, const nlohmann::json*> lib_maps;
  for (const auto& lib : config["libs"]) {
    LOG(INFO) << "lib: " << lib["name"];
    lib_maps[lib["name"]] = &lib;
  }
  std::vector<std::string> php_deps = config["php_config_m4"]["deps"];
  std::set<std::string> php_full_deps;
  for (const auto& dep : php_deps) {
    LOG(INFO) << "dep: " << dep;
    php_full_deps.insert(dep);
    auto it = lib_maps.find(dep);
    if (it != lib_maps.end()) {
      const nlohmann::json* lib = it->second;
      std::vector<std::string> transitive_deps = (*lib)["transitive_deps"];
      php_full_deps.insert(transitive_deps.begin(), transitive_deps.end());
    }
  }
  php_full_deps.erase("z");
  php_full_deps.erase("cares");
  for (const auto& dep : php_full_deps) {
    auto it = lib_maps.find(dep);
    if (it != lib_maps.end()) {
      const nlohmann::json* lib = it->second;
      std::vector<std::string> src = (*lib)["src"];
      srcs.insert(src.begin(), src.end());
    }
  }
  config["php_config_m4"]["srcs"] = srcs;

  std::set<std::string> dirs;
  for (const auto& src : srcs) {
    dirs.insert(src.substr(0, src.rfind('/')));
  }
  config["php_config_m4"]["dirs"] = dirs;
}
