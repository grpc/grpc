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
#include <initializer_list>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "include/nlohmann/json.hpp"
#include "build_metadata.h"
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
      auto type_end = last_space == std::string::npos
                          ? last_star
                          : (last_star == std::string::npos
                                 ? last_space
                                 : std::max(last_space, last_star));
      auto return_type_unstripped = type_and_name.substr(0, type_end + 1);
      auto return_type = absl::StripAsciiWhitespace(return_type_unstripped);
      auto name_unstripped = type_and_name.substr(type_end + 1);
      auto name = absl::StripAsciiWhitespace(name_unstripped);
      auto api = nlohmann::json{{"name", name},
                                {"return_type", return_type},
                                {"arguments", args},
                                {"header", header}};
      apis.push_back(api);
      auto first_slash = header.find('/');
      c_api_headers.insert(header.substr(first_slash + 1));
      header_file = match.suffix();
    }
  }
  config["c_apis"] = apis;
  config["c_api_headers"] = c_api_headers;
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
  std::string php_composer = php_version;
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
  settings["php_version"]["php_debian_version"] = "buster";
  settings["php_version"]["php_composer"] = php_composer;
  settings["python_version"]["pep440"] = pep440;
  settings["ruby_version"]["ruby_version"] = ruby_version;
  
  // Add PHP stability computation
  std::string php_stability = version.tag.has_value() ? "beta" : "stable";
  settings["php_version"]["php_stability"] = php_stability;
  
  // Expand protobuf_version to have the same structure as other versions
  ExpandOneVersion(settings, "protobuf_version");
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
  // Build asm_outputs like the Python version - preserve categories
  nlohmann::json asm_outputs = nlohmann::json::object();
  for (auto it = sources.begin(); it != sources.end(); ++it) {
    const std::string& category = it.key();
    const auto& files = it.value();
    bool has_asm = false;
    for (const auto& file : files) {
      std::string file_str = file;
      if (absl::EndsWith(file_str, ".S") || absl::EndsWith(file_str, ".asm")) {
        has_asm = true;
        break;
      }
    }
    if (has_asm) {
      asm_outputs[category] = files;
    }
  }
  metadata["raw_boringssl_build_output_for_debugging"]["files"] = sources;
  metadata["libs"].push_back(
      {{"name", "boringssl"},
       {"build", "private"},
       {"language", "c"},
       {"secure", false},
       {"src", file_list({"ssl", "crypto"})},
       {"asm_src", [&asm_outputs, &file_list]() {
         nlohmann::json result = nlohmann::json::object();
         for (auto it = asm_outputs.begin(); it != asm_outputs.end(); ++it) {
           const std::string& category = it.key();
           result[category] = file_list({category});
         }
         return result;
       }()},
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

void AddCaresMetadata(nlohmann::json& config) {
  // This translates the contents of src/c-ares/gen_build_yaml.py into C++
  // to match what the Python build system does
  nlohmann::json cares_lib = {
    {"name", "cares"},
    {"defaults", "cares"},
    {"build", "private"},
    {"language", "c"},
    {"secure", false},
    {"src", nlohmann::json::array({
      "third_party/cares/cares/src/lib/ares__read_line.c",
      "third_party/cares/cares/src/lib/ares__get_hostent.c",
      "third_party/cares/cares/src/lib/ares__close_sockets.c",
      "third_party/cares/cares/src/lib/ares__timeval.c",
      "third_party/cares/cares/src/lib/ares_gethostbyaddr.c",
      "third_party/cares/cares/src/lib/ares_getenv.c",
      "third_party/cares/cares/src/lib/ares_free_string.c",
      "third_party/cares/cares/src/lib/ares_free_hostent.c",
      "third_party/cares/cares/src/lib/ares_fds.c",
      "third_party/cares/cares/src/lib/ares_expand_string.c",
      "third_party/cares/cares/src/lib/ares_create_query.c",
      "third_party/cares/cares/src/lib/ares_cancel.c",
      "third_party/cares/cares/src/lib/ares_android.c",
      "third_party/cares/cares/src/lib/ares_parse_txt_reply.c",
      "third_party/cares/cares/src/lib/ares_parse_srv_reply.c",
      "third_party/cares/cares/src/lib/ares_parse_soa_reply.c",
      "third_party/cares/cares/src/lib/ares_parse_ptr_reply.c",
      "third_party/cares/cares/src/lib/ares_parse_ns_reply.c",
      "third_party/cares/cares/src/lib/ares_parse_naptr_reply.c",
      "third_party/cares/cares/src/lib/ares_parse_mx_reply.c",
      "third_party/cares/cares/src/lib/ares_parse_caa_reply.c",
      "third_party/cares/cares/src/lib/ares_options.c",
      "third_party/cares/cares/src/lib/ares_nowarn.c",
      "third_party/cares/cares/src/lib/ares_mkquery.c",
      "third_party/cares/cares/src/lib/ares_llist.c",
      "third_party/cares/cares/src/lib/ares_getsock.c",
      "third_party/cares/cares/src/lib/ares_getnameinfo.c",
      "third_party/cares/cares/src/lib/bitncmp.c",
      "third_party/cares/cares/src/lib/ares_writev.c",
      "third_party/cares/cares/src/lib/ares_version.c",
      "third_party/cares/cares/src/lib/ares_timeout.c",
      "third_party/cares/cares/src/lib/ares_strerror.c",
      "third_party/cares/cares/src/lib/ares_strcasecmp.c",
      "third_party/cares/cares/src/lib/ares_search.c",
      "third_party/cares/cares/src/lib/ares_platform.c",
      "third_party/cares/cares/src/lib/windows_port.c",
      "third_party/cares/cares/src/lib/inet_ntop.c",
      "third_party/cares/cares/src/lib/ares__sortaddrinfo.c",
      "third_party/cares/cares/src/lib/ares__readaddrinfo.c",
      "third_party/cares/cares/src/lib/ares_parse_uri_reply.c",
      "third_party/cares/cares/src/lib/ares__parse_into_addrinfo.c",
      "third_party/cares/cares/src/lib/ares_parse_a_reply.c",
      "third_party/cares/cares/src/lib/ares_parse_aaaa_reply.c",
      "third_party/cares/cares/src/lib/ares_library_init.c",
      "third_party/cares/cares/src/lib/ares_init.c",
      "third_party/cares/cares/src/lib/ares_gethostbyname.c",
      "third_party/cares/cares/src/lib/ares_getaddrinfo.c",
      "third_party/cares/cares/src/lib/ares_freeaddrinfo.c",
      "third_party/cares/cares/src/lib/ares_expand_name.c",
      "third_party/cares/cares/src/lib/ares_destroy.c",
      "third_party/cares/cares/src/lib/ares_data.c",
      "third_party/cares/cares/src/lib/ares__addrinfo_localhost.c",
      "third_party/cares/cares/src/lib/ares__addrinfo2hostent.c",
      "third_party/cares/cares/src/lib/inet_net_pton.c",
      "third_party/cares/cares/src/lib/ares_strsplit.c",
      "third_party/cares/cares/src/lib/ares_strdup.c",
      "third_party/cares/cares/src/lib/ares_send.c",
      "third_party/cares/cares/src/lib/ares_rand.c",
      "third_party/cares/cares/src/lib/ares_query.c",
      "third_party/cares/cares/src/lib/ares_process.c"
    })},
    {"headers", nlohmann::json::array({
      "third_party/cares/ares_build.h",
      "third_party/cares/cares/include/ares_version.h",
      "third_party/cares/cares/include/ares.h",
      "third_party/cares/cares/include/ares_rules.h",
      "third_party/cares/cares/include/ares_dns.h",
      "third_party/cares/cares/include/ares_nameser.h",
      "third_party/cares/cares/src/tools/ares_getopt.h",
      "third_party/cares/cares/src/lib/ares_strsplit.h",
      "third_party/cares/cares/src/lib/ares_android.h",
      "third_party/cares/cares/src/lib/ares_private.h",
      "third_party/cares/cares/src/lib/ares_llist.h",
      "third_party/cares/cares/src/lib/ares_platform.h",
      "third_party/cares/cares/src/lib/ares_ipv6.h",
      "third_party/cares/cares/src/lib/config-dos.h",
      "third_party/cares/cares/src/lib/bitncmp.h",
      "third_party/cares/cares/src/lib/ares_strcasecmp.h",
      "third_party/cares/cares/src/lib/setup_once.h",
      "third_party/cares/cares/src/lib/ares_inet_net_pton.h",
      "third_party/cares/cares/src/lib/ares_data.h",
      "third_party/cares/cares/src/lib/ares_getenv.h",
      "third_party/cares/cares/src/lib/config-win32.h",
      "third_party/cares/cares/src/lib/ares_strdup.h",
      "third_party/cares/cares/src/lib/ares_iphlpapi.h",
      "third_party/cares/cares/src/lib/ares_setup.h",
      "third_party/cares/cares/src/lib/ares_writev.h",
      "third_party/cares/cares/src/lib/ares_nowarn.h",
      "third_party/cares/config_darwin/ares_config.h",
      "third_party/cares/config_freebsd/ares_config.h",
      "third_party/cares/config_linux/ares_config.h",
      "third_party/cares/config_openbsd/ares_config.h"
    })}
  };
  
  config["libs"].push_back(cares_lib);
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

void ExpandSupportedPythonVersions(nlohmann::json& config) {
  auto& settings = config["settings"];
  const auto& supported_python_versions = settings["supported_python_versions"];
  settings["min_python_version"] = supported_python_versions.front();
  settings["max_python_version"] = supported_python_versions.back();
}

void ProcessSwiftPackageFiles(nlohmann::json& config) {
  // Get the swift_package.deps and collect all files, then deduplicate and sort
  if (!config.contains("swift_package") || !config["swift_package"].contains("deps")) {
    return;
  }
  
  auto& swift_package = config["swift_package"];
  auto& deps = swift_package["deps"];
  auto& libs = config["libs"];
  
  std::set<std::string> all_files;
  
  // Collect files from all dependencies
  for (const auto& dep : deps) {
    std::string dep_name = dep.get<std::string>();
    
    // Find the library with this name
    for (const auto& lib : libs) {
      if (lib.contains("name") && lib["name"].get<std::string>() == dep_name) {
        // Add all files from this library
        if (lib.contains("public_headers")) {
          for (const auto& file : lib["public_headers"]) {
            all_files.insert(file.get<std::string>());
          }
        }
        if (lib.contains("headers")) {
          for (const auto& file : lib["headers"]) {
            all_files.insert(file.get<std::string>());
          }
        }
        if (lib.contains("src")) {
          for (const auto& file : lib["src"]) {
            all_files.insert(file.get<std::string>());
          }
        }
        break;
      }
    }
  }
  
  // Convert to sorted JSON array
  nlohmann::json sorted_files = nlohmann::json::array();
  for (const auto& file : all_files) {
    sorted_files.push_back(file);
  }
  
  // Add to swift_package for template use
  swift_package["all_files"] = sorted_files;
}

void ProcessSwiftBoringSSLPackageFiles(nlohmann::json& config) {
  // Get the swift_boringssl_package.deps and collect all files, then deduplicate and sort
  if (!config.contains("swift_boringssl_package") || !config["swift_boringssl_package"].contains("deps")) {
    return;
  }
  
  auto& swift_boringssl_package = config["swift_boringssl_package"];
  auto& deps = swift_boringssl_package["deps"];
  auto& libs = config["libs"];
  
  std::set<std::string> all_files;
  const std::string prefix_to_remove = "third_party/boringssl-with-bazel/";
  
  // Collect files from all dependencies
  for (const auto& dep : deps) {
    std::string dep_name = dep.get<std::string>();
    
    // Find the library with this name
    for (const auto& lib : libs) {
      if (lib.contains("name") && lib["name"].get<std::string>() == dep_name) {
        // Add all src files from this library (BoringSSL template only uses src files)
        if (lib.contains("src")) {
          for (const auto& file : lib["src"]) {
            std::string file_path = file.get<std::string>();
            // Remove the prefix if it exists
            if (absl::StartsWith(file_path, prefix_to_remove)) {
              file_path = file_path.substr(prefix_to_remove.length());
            }
            all_files.insert(file_path);
          }
        }
        break;
      }
    }
  }
  
  // Convert to sorted JSON array
  nlohmann::json sorted_files = nlohmann::json::array();
  for (const auto& file : all_files) {
    sorted_files.push_back(file);
  }
  
  // Add to swift_boringssl_package for template use
  swift_boringssl_package["all_files"] = sorted_files;
}

// Helper function to resolve dependencies and collect files from libraries
// This eliminates duplication across PHP, Ruby, and Python file collection functions
struct DependencyResolver {
  explicit DependencyResolver(const nlohmann::json& config) : config_(config) {
    // Build library maps
    for (const auto& lib : config_["libs"]) {
      std::string lib_name = lib["name"];
      lib_maps_[lib_name] = &lib;
    }
    
    // Get Bazel label to renamed mapping
    bazel_label_to_renamed_ = grpc_tools::artifact_gen::GetBazelLabelToRenamedMapping();
  }
  
  // Expand a list of dependencies to include transitive dependencies
  std::set<std::string> ExpandTransitiveDeps(const std::vector<std::string>& deps, const std::set<std::string>& exclusions = {}) {
    std::set<std::string> full_deps;
    for (const auto& dep : deps) {
      full_deps.insert(dep);
      auto it = lib_maps_.find(dep);
      if (it != lib_maps_.end()) {
        const nlohmann::json* lib = it->second;
        std::vector<std::string> transitive_deps = (*lib)["transitive_deps"];
        full_deps.insert(transitive_deps.begin(), transitive_deps.end());
      }
    }
    
    // Remove exclusions
    for (const auto& exclusion : exclusions) {
      full_deps.erase(exclusion);
    }
    
    return full_deps;
  }
  
  // Collect files from a set of dependencies
  std::set<std::string> CollectFiles(const std::set<std::string>& deps, const std::vector<std::string>& file_types) {
    std::set<std::string> files;
    
    for (const auto& dep : deps) {
      std::string actual_lib_name = dep;
      
      // Check if this is a renamed Bazel label
      auto rename_it = bazel_label_to_renamed_.find(dep);
      if (rename_it != bazel_label_to_renamed_.end()) {
        actual_lib_name = rename_it->second;
      }
      
      auto it = lib_maps_.find(actual_lib_name);
      if (it != lib_maps_.end()) {
        const nlohmann::json* lib = it->second;
        
        // Collect specified file types
        for (const auto& file_type : file_types) {
          if (lib->contains(file_type)) {
            std::vector<std::string> file_list = (*lib)[file_type];
            files.insert(file_list.begin(), file_list.end());
          }
        }
      }
    }
    
    return files;
  }
  
private:
  const nlohmann::json& config_;
  std::map<std::string, const nlohmann::json*> lib_maps_;
  std::map<std::string, std::string> bazel_label_to_renamed_;
};

auto MakePhpConfig(const nlohmann::json& config,
                   std::initializer_list<std::string> remove_libs) {
  DependencyResolver resolver(config);
  std::set<std::string> srcs;
  for (const auto& src : config["php_config_m4"]["src"]) {
    srcs.insert(src.get<std::string>());
  }

  std::vector<std::string> php_deps = config["php_config_m4"]["deps"];
  auto php_full_deps = resolver.ExpandTransitiveDeps(
      php_deps, std::set<std::string>(remove_libs));

  auto dep_files = resolver.CollectFiles(php_full_deps, {"src"});
  srcs.insert(dep_files.begin(), dep_files.end());

  std::set<std::string> dirs;
  for (const auto& src : srcs) {
    dirs.insert(src.substr(0, src.rfind('/')));
  }
  return std::pair(std::move(srcs), std::move(dirs));
}

void AddPhpConfig(nlohmann::json& config) {
  auto [srcs, dirs] = MakePhpConfig(config, {"z", "cares", "@zlib//:zlib"});
  auto [w32_srcs, w32_dirs] = MakePhpConfig(config, {"cares"});

  config["php_config_m4"]["srcs"] = srcs;
  config["php_config_m4"]["dirs"] = dirs;

  std::vector<std::string> windows_srcs;
  for (const auto& src : w32_srcs) {
    windows_srcs.emplace_back(absl::StrReplaceAll(src, {{"/", "\\\\"}}));
  }
  config["php_config_w32"]["srcs"] = windows_srcs;
  std::set<std::string> windows_dirs;
  for (const auto& dir : w32_dirs) {
    windows_dirs.emplace_back(absl::StrReplaceAll(dir, {{"/", "\\\\"}}));
  }
  config["php_config_w32"]["dirs"] = windows_dirs;
}

std::set<std::string> MakePhpPackageXmlSrcs(const nlohmann::json& config) {
  DependencyResolver resolver(config);
  std::set<std::string> srcs;
  
  // Start with php_config_m4.src + php_config_m4.headers (like Python template)
  for (const auto& src : config["php_config_m4"]["src"]) {
    srcs.insert(src);
  }
  for (const auto& header : config["php_config_m4"]["headers"]) {
    srcs.insert(header);
  }
  
  // Get PHP dependencies and expand transitive dependencies
  std::vector<std::string> php_deps = config["php_config_m4"]["deps"];
  auto php_full_deps = resolver.ExpandTransitiveDeps(php_deps, {"cares"});
  
  // Collect public_headers + headers + src files
  auto dep_files = resolver.CollectFiles(php_full_deps, {"public_headers", "headers", "src"});
  srcs.insert(dep_files.begin(), dep_files.end());
  
  return srcs;
}

std::set<std::string> MakeRubyGemFiles(const nlohmann::json& config) {
  DependencyResolver resolver(config);
  
  // Get Ruby dependencies and expand transitive dependencies
  std::vector<std::string> ruby_deps = config["ruby_gem"]["deps"];
  auto ruby_full_deps = resolver.ExpandTransitiveDeps(ruby_deps);
  
  // Collect public_headers + headers + src files
  return resolver.CollectFiles(ruby_full_deps, {"public_headers", "headers", "src"});
}



nlohmann::json MakePythonCoreSourceFiles(const nlohmann::json& config) {
  DependencyResolver resolver(config);
  
  // Get Python dependencies and expand transitive dependencies
  std::vector<std::string> python_deps = config["python_dependencies"]["deps"];
  auto python_full_deps = resolver.ExpandTransitiveDeps(python_deps);
  
  // Collect src files only
  auto srcs = resolver.CollectFiles(python_full_deps, {"src"});
  
  // Convert to sorted JSON array
  nlohmann::json sorted_files = nlohmann::json::array();
  for (const auto& src : srcs) {
    sorted_files.push_back(src);
  }
  
  return sorted_files;
}

nlohmann::json MakePythonAsmSourceFiles(const nlohmann::json& config) {
  DependencyResolver resolver(config);
  nlohmann::json asm_files = nlohmann::json::array();
  
  // Get Python dependencies and expand transitive dependencies
  std::vector<std::string> python_deps = config["python_dependencies"]["deps"];
  auto python_full_deps = resolver.ExpandTransitiveDeps(python_deps);
  
  // For ASM files, we need custom logic since asm_src is a dictionary
  std::map<std::string, const nlohmann::json*> lib_maps;
  for (const auto& lib : config["libs"]) {
    std::string lib_name = lib["name"];
    lib_maps[lib_name] = &lib;
  }
  
  auto bazel_label_to_renamed = grpc_tools::artifact_gen::GetBazelLabelToRenamedMapping();
  
  for (const auto& dep : python_full_deps) {
    std::string actual_lib_name = dep;
    
    // Check if this is a renamed Bazel label
    auto rename_it = bazel_label_to_renamed.find(dep);
    if (rename_it != bazel_label_to_renamed.end()) {
      actual_lib_name = rename_it->second;
    }
    
    auto it = lib_maps.find(actual_lib_name);
    if (it != lib_maps.end()) {
      const nlohmann::json* lib = it->second;
      if (lib->contains("asm_src")) {
        // asm_src is a dictionary in the Python template
        nlohmann::json asm_src = (*lib)["asm_src"];
        for (auto it = asm_src.begin(); it != asm_src.end(); ++it) {
          nlohmann::json asm_entry = nlohmann::json::object();
          asm_entry["asm"] = it.key();
          asm_entry["asm_src"] = it.value();
          asm_files.push_back(asm_entry);
        }
      }
    }
  }
  
  return asm_files;
}

}  // namespace

void AddMetadataForWrappedLanguages(nlohmann::json& config) {
  AddCApis(config);
  AddBoringSslMetadata(config);
  AddAbseilMetadata(config);
  AddCaresMetadata(config);
  ExpandTransitiveDeps(config);
  ExpandVersion(config);
  AddSupportedBazelVersions(config);
  ExpandSupportedPythonVersions(config);
  ProcessSwiftPackageFiles(config);
  ProcessSwiftBoringSSLPackageFiles(config);
  AddPhpConfig(config);
  
  // Add package.xml-specific file collection
  auto package_xml_srcs = MakePhpPackageXmlSrcs(config);
  nlohmann::json package_xml_files = nlohmann::json::array();
  for (const auto& src : package_xml_srcs) {
    package_xml_files.push_back(src);
  }
  config["package_xml_srcs"] = package_xml_files;
  
  // Add Ruby gem file collection
  auto ruby_gem_files = MakeRubyGemFiles(config);
  nlohmann::json ruby_gem_files_array = nlohmann::json::array();
  for (const auto& file : ruby_gem_files) {
    ruby_gem_files_array.push_back(file);
  }
  config["ruby_gem_files"] = ruby_gem_files_array;
  
  // Add Python core dependencies file collection
  auto python_core_files = MakePythonCoreSourceFiles(config);
  auto python_asm_files = MakePythonAsmSourceFiles(config);
  config["python_core_source_files"] = python_core_files;
  config["python_asm_source_files"] = python_asm_files;
}