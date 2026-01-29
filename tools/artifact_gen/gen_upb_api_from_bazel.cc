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

// Generates upb source files (*.upb.c, *.upb.h, etc.) from all upb
// targets in Bazel BUILD files. These generated files are used for non-Bazel
// builds like make and CMake.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "pugixml.hpp"

// Command-line flags
ABSL_FLAG(bool, verbose, false, "Enable verbose output.");
ABSL_FLAG(std::string, upb_out, "src/core/ext/upb-gen",
          "Output directory for upb targets");
ABSL_FLAG(std::string, upbdefs_out, "src/core/ext/upbdefs-gen",
          "Output directory for upbdefs targets");
ABSL_FLAG(std::string, mode, "generate_and_copy",
          "The mode to run in: "
          "'generate_and_copy', 'list_deps', 'clean' or 'list_build_targets'");
ABSL_FLAG(std::string, upb_rules_xml, "",
          "Path to the XML file from `bazel query` on upb rules.");
ABSL_FLAG(std::string, deps_xml, "",
          "Path to the XML file from `bazel query` on upb rule deps.");

// Represents a Bazel rule
struct Rule {
  std::string name;
  std::string type;
  std::vector<std::string> srcs;
  std::vector<std::string> deps;
  std::vector<std::string> proto_files;
};

std::string ReadFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    LOG(FATAL) << "Could not open file: " << path;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::map<std::string, Rule> ParseBazelRules(
    const std::string& xml_string, const std::set<std::string>& rule_types) {
  std::map<std::string, Rule> rules;
  pugi::xml_document doc;
  if (!doc.load_string(xml_string.c_str())) {
    LOG(FATAL) << "Failed to parse xml: " << xml_string;
  }

  for (pugi::xml_node rule_node : doc.child("query").children("rule")) {
    std::string rule_class = rule_node.attribute("class").as_string();
    if (!rule_types.empty() && rule_types.find(rule_class) == rule_types.end()) {
      continue;
    }

    Rule r;
    r.type = rule_class;
    r.name = rule_node.attribute("name").as_string();

    for (const auto& child : rule_node.children("list")) {
      auto list_name = child.attribute("name").as_string();
      std::vector<std::string>* target_list = nullptr;
      if (strcmp(list_name, "srcs") == 0) {
        target_list = &r.srcs;
      } else if (strcmp(list_name, "deps") == 0) {
        target_list = &r.deps;
      }
      if (target_list != nullptr) {
        for (const auto& label : child.children("label")) {
          target_list->push_back(label.attribute("value").as_string());
        }
      }
    }

    // Handle alias
    for (const auto& child : rule_node.children("label")) {
      if (strcmp(child.attribute("name").as_string(), "actual") == 0) {
        auto actual_name = child.attribute("value").as_string();
        if (actual_name != nullptr && strlen(actual_name) > 0) {
          r.deps.push_back(actual_name);
        }
      }
    }

    rules[r.name] = r;
  }
  return rules;
}

std::vector<Rule> GetUpbRules(const std::string& upb_rules_xml_path) {
  std::string query_result = ReadFile(upb_rules_xml_path);
  auto upb_rules_map = ParseBazelRules(
      query_result, {"upb_c_proto_library", "upb_proto_reflection_library"});
  std::vector<Rule> upb_rules;
  for (auto const& [name, rule] : upb_rules_map) {
    upb_rules.push_back(rule);
  }
  return upb_rules;
}

std::vector<std::string> GetTransitiveProtos(
    const std::map<std::string, Rule>& rules, const std::string& start_node) {
  std::vector<std::string> queue;
  queue.push_back(start_node);
  std::set<std::string> visited;
  visited.insert(start_node);
  std::set<std::string> proto_files_set;

  while (!queue.empty()) {
    std::string current_name = queue.front();
    queue.erase(queue.begin());

    auto it = rules.find(current_name);
    if (it != rules.end()) {
      const Rule& rule = it->second;
      for (const auto& dep : rule.deps) {
        if (visited.find(dep) == visited.end()) {
          visited.insert(dep);
          queue.push_back(dep);
        }
      }
      for (const auto& src : rule.srcs) {
        if (absl::EndsWith(src, ".proto")) {
          proto_files_set.insert(src);
        }
      }
    }
  }
  std::vector<std::string> result(proto_files_set.begin(),
                                  proto_files_set.end());
  return result;
}

std::string GetUpbPath(std::string proto_path, const std::string& ext) {
  absl::StrReplaceAll({{":", "/"}}, &proto_path);
  return absl::StrReplaceAll(proto_path, {{".proto", ext}});
}

std::pair<std::string, std::string> GetExternalLink(const std::string& file) {
  const std::vector<std::pair<std::string, std::string>> kExternalLinks = {
      {"@com_google_protobuf//", "src/"},
      {"@com_google_googleapis//", ""},
      {"@com_github_cncf_xds//", ""},
      {"@com_envoyproxy_protoc_gen_validate//", ""},
      {"@dev_cel//", "proto/"},
      {"@envoy_api//", ""},
      {"@opencensus_proto//", ""},
  };
  for (const auto& link : kExternalLinks) {
    if (absl::StartsWith(file, link.first)) {
      return link;
    }
  }
  return {"//", ""};
}

std::string GetBazelBinRootPath(
    const std::pair<std::string, std::string>& elink, const std::string& file) {
  const std::string kBazelBinRoot = "bazel-bin/";
  if (elink.first == "@com_google_protobuf//") {
    std::string name_part = std::filesystem::path(file).stem().string();
    // For upb generated files, we need to strip two extensions.
    name_part = std::filesystem::path(name_part).stem().string();
    return absl::StrCat(
        kBazelBinRoot, "external/",
        absl::StrReplaceAll(elink.first, {{"@", ""}, {"//", ""}}),
        "/src/google/protobuf/_virtual_imports/", name_part, "_proto/", file);
  }
  if (elink.first == "@dev_cel//") {
    std::string name_part = std::filesystem::path(file).stem().string();
    // For upb generated files, we need to strip two extensions.
    name_part = std::filesystem::path(name_part).stem().string();
    return absl::StrCat(
        kBazelBinRoot, "external/",
        absl::StrReplaceAll(elink.first, {{"@", ""}, {"//", ""}}),
        "/proto/cel/expr/_virtual_imports/", name_part, "_proto/", file);
  }
  if (absl::StartsWith(elink.first, "@")) {
    return absl::StrCat(
        kBazelBinRoot, "external/",
        absl::StrReplaceAll(elink.first, {{"@", ""}, {"//", ""}}), "/",
        elink.second, file);
  } else {
    return absl::StrCat(kBazelBinRoot, file);
  }
}

void CopyFile(const std::string& src, const std::string& dest) {
  std::error_code ec;
  std::filesystem::path dest_path(dest);
  if (dest_path.has_parent_path()) {
    std::filesystem::create_directories(dest_path.parent_path(), ec);
    if (ec) {
      std::cerr << "Filesystem error creating directories for " << dest << ": "
                << ec.message() << std::endl;
      exit(1);
    }
  }
  
  // Copy file content without preserving permissions (like Python's shutil.copyfile)
  std::ifstream src_file(src, std::ios::binary);
  if (!src_file) {
    std::cerr << "Error: Cannot open source file: " << src << std::endl;
    exit(1);
  }
  
  std::ofstream dest_file(dest_path, std::ios::binary);
  if (!dest_file) {
    std::cerr << "Error: Cannot open destination file: " << dest << std::endl;
    exit(1);
  }
  
  dest_file << src_file.rdbuf();
  
  if (!dest_file.good() || !src_file.good()) {
    std::cerr << "Error: Failed to copy file content from " << src << " to " << dest << std::endl;
    exit(1);
  }
}

void CopyUpbGeneratedFiles(std::vector<Rule>& rules, bool verbose,
                           const std::string& upb_out,
                           const std::string& upbdefs_out,
                           const std::string& deps_xml_path) {
  std::string deps_xml = ReadFile(deps_xml_path);
  auto all_rules = ParseBazelRules(deps_xml, {});

  for (auto& rule : rules) {
    if (rule.deps.size() == 1) {
      rule.proto_files = GetTransitiveProtos(all_rules, rule.deps[0]);
    }
  }

  std::map<std::string, std::string> files_to_copy;
  for (const auto& rule : rules) {
    const auto& extensions = (rule.type == "upb_c_proto_library")
                                 ? std::vector<std::string>{".upb.h", ".upb_minitable.h",
                                                    ".upb_minitable.c"}
                                 : std::vector<std::string>{".upbdefs.h", ".upbdefs.c"};
    const auto& output_dir =
        (rule.type == "upb_c_proto_library") ? upb_out : upbdefs_out;
    for (const auto& proto_file_raw : rule.proto_files) {
      auto elink = GetExternalLink(proto_file_raw);
      std::string prefix_to_strip = elink.first + elink.second;
      if (!absl::StartsWith(proto_file_raw, prefix_to_strip)) {
        std::cerr << "Source file \"" << proto_file_raw
                  << "\" does not have the expected prefix \""
                  << prefix_to_strip << "\"" << std::endl;
        exit(1);
      }
      std::string proto_file = proto_file_raw.substr(prefix_to_strip.length());
      for (const auto& ext : extensions) {
        std::string file = GetUpbPath(proto_file, ext);
        std::string src = GetBazelBinRootPath(elink, file);
        std::string dest = output_dir + "/" + file;
        files_to_copy[src] = dest;
      }
    }
  }

  for (const auto& pair : files_to_copy) {
    if (verbose) {
      std::cout << "Copy:" << std::endl;
      std::cout << "    " << pair.first << std::endl;
      std::cout << " -> " << pair.second << std::endl;
    }
    CopyFile(pair.first, pair.second);
  }
}

std::vector<std::string> GetBuildTargets(const std::vector<Rule>& rules,
                                         const std::string& deps_xml_path) {
  std::vector<std::string> build_targets;
  for (const auto& rule : rules) {
    build_targets.push_back(rule.name);
  }
  return build_targets;
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  bool verbose = absl::GetFlag(FLAGS_verbose);
  std::string upb_out = absl::GetFlag(FLAGS_upb_out);
  std::string upbdefs_out = absl::GetFlag(FLAGS_upbdefs_out);
  std::string mode = absl::GetFlag(FLAGS_mode);
  std::string upb_rules_xml = absl::GetFlag(FLAGS_upb_rules_xml);
  std::string deps_xml = absl::GetFlag(FLAGS_deps_xml);

  if (mode == "clean") {
    std::filesystem::remove_all(upb_out);
    std::filesystem::remove_all(upbdefs_out);
    return 0;
  }

  auto upb_rules = GetUpbRules(upb_rules_xml);

  if (mode == "list_deps") {
    std::set<std::string> all_deps;
    for (const auto& rule : upb_rules) {
      for (const auto& dep : rule.deps) {
        all_deps.insert(dep);
      }
    }
    std::cout << absl::StrJoin(all_deps, " ");
  } else if (mode == "list_build_targets") {
    auto targets = GetBuildTargets(upb_rules, deps_xml);
    std::cout << absl::StrJoin(targets, " ");
  } else if (mode == "generate_and_copy") {
    CopyUpbGeneratedFiles(upb_rules, verbose, upb_out, upbdefs_out, deps_xml);
  } else {
    std::cerr << "Invalid mode: " << mode << std::endl;
    return 1;
  }

  return 0;
}
