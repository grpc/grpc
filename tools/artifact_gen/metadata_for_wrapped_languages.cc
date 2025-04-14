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
#include <regex>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/str_join.h"
#include "absl/strings/escaping.h"
#include "utils.h"

namespace {
void AddCApis(nlohmann::json& config) {
  std::set<std::string> headers;
  for (const auto& lib : config["libs"]) {
    if (lib["name"] == "grpc" || lib["name"] == "gpr") {
      for (const auto& header : lib["public_headers"]) {
        headers.insert(header);
      }
    }
  }
  std::regex re_api(R"((?:GPRAPI|GRPCAPI|CENSUSAPI)([^#;]*);)");
  std::vector<nlohmann::json> apis;
  std::set<std::string> c_api_headers;
  for (const auto& header : headers) {
    auto header_file = LoadString("../../" + header);
    for (std::smatch match; std::regex_search(header_file, match, re_api);) {
      std::string api_declaration = match[1];
      for (char& c : api_declaration) {
        if (c == '\t' || c == '\n') {
          c = ' ';
        }
      }
      absl::RemoveExtraAsciiWhitespace(&api_declaration);
      auto first_paren = api_declaration.find('(');
      auto type_and_name = api_declaration.substr(0, first_paren);
      auto args_and_close = api_declaration.substr(first_paren + 1);
      args_and_close = args_and_close.substr(0, args_and_close.rfind(')'));
      auto args = absl::StripAsciiWhitespace(args_and_close);
      auto last_space = type_and_name.rfind(' ');
      auto last_star = type_and_name.rfind('*');
      auto type_end = last_space == std::string::npos ? last_star : (
        last_star == std::string::npos ? last_space : std::max(last_space, last_star)
      );
      auto return_type_unstripped = type_and_name.substr(0, type_end + 1);
      auto return_type = absl::StripAsciiWhitespace(return_type_unstripped);
      auto name_unstripped = type_and_name.substr(type_end + 1);
      auto name = absl::StripAsciiWhitespace(name_unstripped);
      auto api = nlohmann::json{
        {"name", name},
        {"return_type", return_type},
        {"arguments", args},
        {"header", header}
      };
      apis.push_back(api);
      auto first_slash = header.find('/');
      c_api_headers.insert(header.substr(first_slash + 1));
      header_file = match.suffix();
    }
  }
  config["c_apis"] = apis;
  config["c_api_headers"] = c_api_headers;
}

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

struct Version {
  int major;
  int minor;
  int patch;
  std::optional<std::string> tag;
};

Version ExpandOneVersion(nlohmann::json& settings, const std::string& which) {
  std::string version_string = settings[which];
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
  settings[which] = nlohmann::json::object();
  settings[which]["string"] = version_string;
  settings[which]["major"] = major;
  settings[which]["minor"] = minor;
  settings[which]["patch"] = patch;
  if (tag.has_value()) {
    settings[which]["tag"] = *tag;
    settings[which]["string"] = absl::StrCat(version_string, "-", *tag);
  }
  return Version{major, minor, patch, tag};
}

void ExpandVersion(nlohmann::json& config) {
  auto& settings = config["settings"];
  auto version = ExpandOneVersion(settings, "version");
  std::string php_version =
      absl::StrCat(version.major, ".", version.minor, ".", version.patch);
  if (version.tag.has_value()) {
    if (version.tag == "dev") {
      php_version += "dev";
    } else if (version.tag->size() >= 3 && version.tag->substr(0, 3) == "pre") {
      php_version += "RC" + version.tag->substr(3);
    } else {
      LOG(FATAL) << "Unknown tag: " << *version.tag;
    }
  }
  std::string ruby_version =
      absl::StrCat(version.major, ".", version.minor, ".", version.patch);
  if (version.tag.has_value()) {
    ruby_version += "." + *version.tag;
  }
  std::string pep440 =
      absl::StrCat(version.major, ".", version.minor, ".", version.patch);
  if (version.tag.has_value()) {
    if (*version.tag == "dev") {
      pep440 += ".dev0";
    } else if (absl::StartsWith(*version.tag, "pre")) {
      pep440 += absl::StrCat("rc", version.tag->substr(3));
    } else {
      LOG(FATAL) << "Don\'t know how to translate version tag " << *version.tag
                 << " to pep440";
    }
  }
  for (std::string language :
       {"cpp", "csharp", "node", "objc", "php", "python", "ruby"}) {
    std::string version_tag = absl::StrCat(language, "_version");
    Version v = version;
    if (auto override_major =
            settings.find(absl::StrCat(language, "_major_version"));
        override_major != settings.end()) {
      std::string override_value = *override_major;
      CHECK(absl::SimpleAtoi(override_value, &v.major));
    }
    settings[version_tag] = nlohmann::json::object();
    settings[version_tag]["string"] =
        absl::StrCat(v.major, ".", v.minor, ".", v.patch,
                     v.tag.has_value() ? absl::StrCat("-", *v.tag) : "");
    settings[version_tag]["major"] = v.major;
    settings[version_tag]["minor"] = v.minor;
    settings[version_tag]["patch"] = v.patch;
    settings[version_tag]["tag_or_empty"] = v.tag.value_or("");
  }
  settings["version"]["php"] = php_version;
  ExpandOneVersion(settings, "core_version");
  settings["php_version"]["php_current_version"] = "8.1";
  settings["python_version"]["pep440"] = pep440;
  settings["ruby_version"]["ruby_version"] = ruby_version;
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

void AddSupportedBazelVersions(nlohmann::json& config) {
  std::ifstream file("../../bazel/supported_versions.txt");
  std::string line;
  std::vector<std::string> versions;
  while (std::getline(file, line)) {
    line = absl::StripAsciiWhitespace(line);
    if (line.empty()) continue;
    versions.push_back(line);
  }
  config["supported_bazel_versions"] = versions;
  config["primary_bazel_version"] = versions.front();
}
}  // namespace

void AddMetadataForWrappedLanguages(nlohmann::json& config) {
  AddCApis(config);
  AddBoringSslMetadata(config);
  AddAbseilMetadata(config);
  ExpandTransitiveDeps(config);
  AddPhpConfig(config);
  ExpandVersion(config);
  AddSupportedBazelVersions(config);
}
