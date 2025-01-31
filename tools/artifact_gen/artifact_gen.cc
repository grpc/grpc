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

#include <map>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "pugixml.hpp"

struct ExternalProtoLibrary {
  std::string destination;
  std::string proto_prefix;
};

struct BazelRule {
  std::string clazz;
  std::string name;
  std::vector<std::string> srcs;
  std::vector<std::string> hdrs;
  std::vector<std::string> textual_hdrs;
  std::vector<std::string> deps;
  std::vector<std::string> data;
  std::vector<std::string> tags;
  std::vector<std::string> args;
  std::optional<std::string> generator_function;
  std::optional<std::string> size;
  bool flaky;
  std::optional<std::string> actual;  // the real target name for aliases

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const BazelRule& bazel_rule) {
    absl::Format(&sink,
                 "%s(name=%s, srcs=[%s], hdrs=[%s], textual_hdrs=[%s], "
                 "deps=[%s], data=[%s], tags=[%s], args=[%s], "
                 "generator_function=%s, size=%s, flaky=%s, actual=%s)",
                 bazel_rule.clazz, bazel_rule.name,
                 absl::StrJoin(bazel_rule.srcs, ","),
                 absl::StrJoin(bazel_rule.hdrs, ","),
                 absl::StrJoin(bazel_rule.textual_hdrs, ","),
                 absl::StrJoin(bazel_rule.deps, ","),
                 absl::StrJoin(bazel_rule.data, ","),
                 absl::StrJoin(bazel_rule.tags, ","),
                 absl::StrJoin(bazel_rule.args, ","),
                 bazel_rule.generator_function.has_value()
                     ? *bazel_rule.generator_function
                     : "None",
                 bazel_rule.size.has_value() ? *bazel_rule.size : "None",
                 bazel_rule.flaky ? "True" : "False",
                 bazel_rule.actual.has_value() ? *bazel_rule.actual : "None");
  }
};

BazelRule BazelRuleFromXml(const pugi::xml_node& node) {
  BazelRule out;
  out.clazz = node.attribute("class").as_string();
  out.name = node.attribute("name").as_string();
  for (const auto& child : node.children("list")) {
    auto match = [&child](absl::string_view name,
                          std::vector<std::string>* out) {
      if (child.attribute("name").as_string() != name) return;
      for (const auto& label : child.children("label")) {
        out->push_back(label.attribute("value").as_string());
      }
    };
    match("srcs", &out.srcs);
    match("hdrs", &out.hdrs);
    match("textual_hdrs", &out.textual_hdrs);
    match("deps", &out.deps);
    match("data", &out.data);
    match("tags", &out.tags);
    match("args", &out.args);
  }
  for (const auto& child : node.children("string")) {
    auto match = [&child](absl::string_view name,
                          std::optional<std::string>* out) {
      if (child.attribute("name").as_string() != name) return;
      out->emplace(child.attribute("value").as_string());
    };
    match("generator_function", &out.generator_function);
    match("size", &out.size);
  }
  for (const auto& child : node.children("boolean")) {
    auto match = [&child](absl::string_view name, bool* out) {
      if (child.attribute("name").as_string() != name) return;
      *out = 0 == strcmp(child.attribute("value").as_string(), "true");
    };
    match("flaky", &out.flaky);
  }
  for (const auto& child : node.children("label")) {
    // extract actual name for alias and bind rules
    if (0 == strcmp(child.attribute("name").as_string(), "actual")) {
      out.actual = child.attribute("value").as_string();
      out.deps.push_back(*out.actual);
    }
  }
  return out;
}

class ArtifactGen {
 public:
  void LoadRulesXml(const char* source) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(source);
    CHECK(result) << source;

    for (const auto& query : doc.children("query")) {
      for (const auto& child : query.children("rule")) {
        auto bazel_rule = BazelRuleFromXml(child);
        if (bazel_rule.clazz == "cc_library" ||
            bazel_rule.clazz == "cc_binary" || bazel_rule.clazz == "cc_test" ||
            bazel_rule.clazz == "cc_proto_library" ||
            bazel_rule.clazz == "cc_proto_gen_validate" ||
            bazel_rule.clazz == "proto_library" ||
            bazel_rule.clazz == "upb_c_proto_library" ||
            bazel_rule.clazz == "upb_proto_reflection_library" ||
            bazel_rule.clazz == "alias" || bazel_rule.clazz == "bind" ||
            bazel_rule.clazz == "genrule") {
          rules_[bazel_rule.name] = bazel_rule;
        }
      }
    }
  }

  void ExpandUpbProtoLibraryRules() {
    const std::string kGenUpbRoot = "//:src/core/ext/upb-gen/";
    const std::string kGenUpbdefsRoot = "//:src/core/ext/upbdefs-gen/";
    const std::map<std::string, std::string> kExternalLinks{
        {"@com_google_protobuf//", "src/"},
        {"@com_google_googleapis//", ""},
        {"@com_github_cncf_xds//", ""},
        {"@com_envoyproxy_protoc_gen_validate//", ""},
        {"@envoy_api//", ""},
        {"@opencensus_proto//", ""},
    };
    for (auto& [name, bazel_rule] : rules_) {
      if (bazel_rule.generator_function != "grpc_upb_proto_library" &&
          bazel_rule.generator_function !=
              "grpc_upb_proto_reflection_library") {
        continue;
      }
      CHECK_EQ(bazel_rule.deps.size(), 1u) << bazel_rule;
      const std::string original_dep = bazel_rule.deps[0];
      // deps is not properly fetched from bazel query for upb_c_proto_library
      // target so add the upb dependency manually
      bazel_rule.deps = {
          "@com_google_protobuf//upb:descriptor_upb_proto",
          "@com_google_protobuf//"
          "upb:generated_code_support__only_for_generated_code_do_not_use__i_"
          "give_permission_to_break_me",
      };
      // populate the upb_c_proto_library rule with pre-generated upb headers
      // and sources using proto_rule
      const auto protos = GetTransitiveProtos(original_dep);
      CHECK_NE(protos.size(), 0u);
      std::vector<std::string> files;
      for (std::string proto_src : protos) {
        for (const auto& [prefix, expected_dir] : kExternalLinks) {
          if (absl::StartsWith(proto_src, prefix)) {
            std::string prefix_to_strip = prefix + expected_dir;
            CHECK(absl::StartsWith(proto_src, prefix_to_strip))
                << "Source file " << proto_src << " in upb rule " << name
                << " does not have the expected prefix " << prefix_to_strip;
            proto_src = proto_src.substr(prefix.length());
          }
        }
        CHECK(!absl::StartsWith(proto_src, "@"))
            << name << " is unknown workspace; proto_src=" << proto_src;
        const std::string proto_src_file =
            TryExtractSourceFilePath(proto_src).value();
        std::vector<std::string> extensions;
        std::string root;
        if (bazel_rule.generator_function == "grpc_upb_proto_library") {
          extensions = {".upb.h", ".upb_minitable.h", ".upb_minitable.c"};
          root = kGenUpbRoot;
        } else {
          extensions = {".upbdefs.h", ".upbdefs.c"};
          root = kGenUpbdefsRoot;
        }
        for (const auto& ext : extensions) {
          files.push_back(absl::StrCat(
              root, absl::StrReplaceAll(proto_src_file, {{".proto", ext}})));
        }
      }
      bazel_rule.srcs = files;
      bazel_rule.hdrs = files;
    }
  }

 private:
  std::set<std::string> GetTransitiveProtos(std::string root) {
    std::queue<std::string> todo;
    std::set<std::string> visited;
    std::set<std::string> ret;
    todo.push(root);
    while (!todo.empty()) {
      std::string name = todo.front();
      todo.pop();
      auto rule_it = rules_.find(name);
      if (rule_it == rules_.end()) continue;
      const auto& rule = rule_it->second;
      for (const auto& dep : rule.deps) {
        if (visited.emplace(dep).second) todo.push(dep);
      }
      for (const auto& src : rule.srcs) {
        if (absl::EndsWith(src, ".proto")) ret.insert(src);
      }
    }
    return ret;
  }

  std::optional<std::string> TryExtractSourceFilePath(std::string label) {
    if (absl::StartsWith(label, "@")) {
      // This is an external source file. We are only interested in sources
      // for some of the external libraries.
      for (const auto& [lib_name, prefix] : external_source_prefixes_) {
        if (absl::StartsWith(label, lib_name)) {
          return absl::StrReplaceAll(
              absl::StrCat(prefix, label.substr(lib_name.length())),
              {{":", "/"}, {"//", "/"}});
        }
      }
      // This source file is external, and we need to translate the
      // @REPO_NAME to a valid path prefix. At this stage, we need
      // to check repo name, since the label/path mapping is not
      // available in BUILD files.
      for (const auto& [lib_name, external_proto_lib] :
           external_proto_libraries_) {
        if (absl::StartsWith(label, "@" + lib_name + "//")) {
          return absl::StrReplaceAll(
              absl::StrCat(external_proto_lib.proto_prefix,
                           label.substr(lib_name.length() + 3)),
              {{":", "/"}});
        }
      }
      // No external library match found.
      return std::nullopt;
    }
    if (absl::StartsWith(label, "//")) label = label.substr(2);
    if (absl::StartsWith(label, ":")) label = label.substr(1);
    return absl::StrReplaceAll(label, {{":", "/"}});
  }

  std::map<std::string, BazelRule> rules_;
  const std::map<std::string, std::string> external_source_prefixes_ = {
      // TODO(veblush) : Remove @utf8_range// item once protobuf is upgraded
      // to 26.x
      {"@utf8_range//", "third_party/utf8_range"},
      {"@com_googlesource_code_re2//", "third_party/re2"},
      {"@com_google_googletest//", "third_party/googletest"},
      {"@com_google_protobuf//upb", "third_party/upb/upb"},
      {"@com_google_protobuf//third_party/utf8_range",
       "third_party/utf8_range"},
      {
          "@zlib//",
          "third_party/zlib",
      },
  };
  const std::map<std::string, ExternalProtoLibrary> external_proto_libraries_ =
      {{"envoy_api",
        {
            "third_party/envoy-api",
            "third_party/envoy-api/",
        }},
       {"com_google_googleapis",
        {
            "third_party/googleapis",
            "third_party/googleapis/",
        }},
       {"com_github_cncf_xds", {"third_party/xds", "third_party/xds/"}},
       {"com_envoyproxy_protoc_gen_validate",
        {
            "third_party/protoc-gen-validate",
            "third_party/protoc-gen-validate/",
        }},
       {
           "opencensus_proto",
           {
               "third_party/opencensus-proto/src",
               "third_party/opencensus-proto/src/",
           },
       }};
};

int main(int argc, char** argv) {
  ArtifactGen generator;
  for (int i = 1; i < argc; ++i) {
    generator.LoadRulesXml(argv[i]);
  }
  generator.ExpandUpbProtoLibraryRules();
  return 0;
}
