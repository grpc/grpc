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

#include "metadata_for_wrapped_languages.h"

#include <fstream>
#include <optional>
#include <set>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/str_join.h"
#include "utils.h"

namespace {
void AddPhpConfig(nlohmann::json& config) {
  std::set<std::string> srcs;
  for (const auto& src : config["php_config_m4"]["src"]) {
    srcs.insert(src);
  }
  std::map<std::string, const nlohmann::json*> lib_maps;
  for (const auto& lib : config["libs"]) {
    lib_maps[lib["name"]] = &lib;
  }
  std::vector<std::string> php_deps = config["php_config_m4"]["deps"];
  std::set<std::string> php_full_deps;
  for (const auto& dep : php_deps) {
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
  php_full_deps.erase("@zlib//:zlib");
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

void AddBoringSslMetadata(nlohmann::json& metadata) {
  std::ifstream sources_in(
      "../../third_party/boringssl-with-bazel/sources.json");
  auto sources = nlohmann::json::parse(sources_in);
  auto file_list = [&sources](std::initializer_list<std::string> sections) {
    std::vector<std::string> ret;
    for (const auto& section : sections) {
      const auto& files = sources[section];
      for (const auto& file : files) {
        std::string file_str = file;
        ret.push_back(
            absl::StrCat("third_party/boringssl-with-bazel/", file_str));
      }
    }
    std::sort(ret.begin(), ret.end());
    return ret;
  };
  std::vector<std::string> asm_outputs;
  for (auto it = sources.begin(); it != sources.end(); ++it) {
    for (const auto& file : it.value()) {
      std::string file_str = file;
      if (absl::EndsWith(file_str, ".S") || absl::EndsWith(file_str, ".asm")) {
        asm_outputs.push_back(file);
      }
    }
  }
  metadata["raw_boringssl_build_output_for_debugging"]["files"] = sources;
  metadata["libs"].push_back(
      {{"name", "boringssl"},
       {"build", "private"},
       {"language", "c"},
       {"secure", false},
       {"src", file_list({"ssl", "crypto"})},
       {"asm_src", file_list({"asm"})},
       {"headers",
        file_list({"ssl_headers", "ssl_internal_headers", "crypto_headers",
                   "crypto_internal_headers", "fips_fragments"})},
       {"boringssl", true},
       {"defaults", "boringssl"}});
  metadata["libs"].push_back({{"name", "boringssl_test_util"},
                              {"build", "private"},
                              {"language", "c++"},
                              {"secure", false},
                              {"boringssl", true},
                              {"defaults", "boringssl"},
                              {"src", file_list({"test_support"})}});
  for (const auto& test : {"ssl_test", "crypto_test"}) {
    metadata["targets"].push_back(
        {{"name", absl::StrCat("boringssl_", test)},
         {"build", "test"},
         {"run", false},
         {"secure", false},
         {"language", "c++"},
         {"src", file_list({test})},
         {"boringssl", true},
         {"defaults", "boringssl"},
         {"deps", {"boringssl_test_util", "boringssl"}}});
    metadata["tests"].push_back({
        {"name", absl::StrCat("boringssl_", test)},
        {"args", {}},
        {"exclude_configs", {"asan", "ubsan"}},
        {"ci_platforms", {"linux", "mac", "posix", "windows"}},
        {"platforms", {"linux", "mac", "posix", "windows"}},
        {"flaky", false},
        {"gtest", true},
        {"language", "c++"},
        {"boringssl", true},
        {"defaults", "boringssl"},
    });
  }
}

void AddAbseilMetadata(nlohmann::json& config) {
  auto preprocessed = LoadYaml("../../src/abseil-cpp/preprocessed_builds.yaml");
  for (auto& build : preprocessed) {
    build["build"] = "private";
    build["build_system"] = nlohmann::json::array();
    build["language"] = "c";
    build["secure"] = false;
    config["libs"].push_back(build);
  }
}

class TransitiveDepsCalculator {
public:
  void DeclareDeps(std::string name, std::set<std::string> deps) {
    auto& dst = deps_[name];
    for (const auto& dep : deps) dst.insert(dep);
  }

  std::set<std::string> Calculate(std::string which) {
    std::set<std::string> deps;
    Fill(which, &deps);
    return deps;
  }

private:
  void Fill(std::string which, std::set<std::string>* out) {
    auto it = deps_.find(which);
    if (it == deps_.end()) return;
    for (const auto& dep : it->second) {
      if (out->emplace(dep).second) {
        Fill(dep, out);
      }
    }
  }

  std::map<std::string, std::set<std::string>> deps_;
};

void ExpandTransitiveDeps(nlohmann::json& config) {
  TransitiveDepsCalculator calc;
  for (auto& lib : config["libs"]) {
    calc.DeclareDeps(lib["name"], {});
    auto grab = [&lib, &calc](std::string tag) {
      auto value = lib[tag];
      if (!value.is_array()) {
        if (!value.is_null()) {
          LOG(INFO) << lib["name"] << " " << tag << " " << value;
        }
        return;
      }
      calc.DeclareDeps(lib["name"], value);
    };
    grab("transitive_deps");
    grab("deps");
  }
  for (auto& lib : config["libs"]) {
    lib["transitive_deps"] = calc.Calculate(lib["name"]);
  }
}
}  // namespace

void AddMetadataForWrappedLanguages(nlohmann::json& config) {
  AddBoringSslMetadata(config);
  AddAbseilMetadata(config);
  ExpandTransitiveDeps(config);
  AddPhpConfig(config);
  ExpandVersion(config);
}
