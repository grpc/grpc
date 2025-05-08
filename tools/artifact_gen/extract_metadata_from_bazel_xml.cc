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

#include <fstream>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "include/nlohmann/json.hpp"
#include "pugixml.hpp"

ABSL_FLAG(std::vector<std::string>, target_query, {},
          "Filename containing bazel query results for some set of targets");
ABSL_FLAG(std::string, external_http_archive_query, "",
          "Filename containing bazel query results for external http archives");

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
  bool flaky = false;
  std::optional<std::string> actual;  // the real target name for aliases

  bool transitive_deps_computed = false;
  std::set<std::string> transitive_deps;
  std::set<std::string> collapsed_deps;
  std::set<std::string> exclude_deps;
  std::set<std::string> collapsed_srcs;
  std::set<std::string> collapsed_public_headers;
  std::set<std::string> collapsed_headers;

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
                 bazel_rule.flaky ? "true" : "true",
                 bazel_rule.actual.has_value() ? *bazel_rule.actual : "None");
  }
};

struct HttpArchive {
  std::string name;
  std::vector<std::string> urls;
  std::string sha256;
  std::string strip_prefix;
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

// extra metadata that will be used to construct build.yaml
// there are mostly extra properties that we weren't able to obtain from the
// bazel build _TYPE: whether this is library, target or test _RENAME: whether
// this target should be renamed to a different name (to match expectations of
// make and cmake builds)
static const char* kBuildExtraMetadata = R"json({
    "third_party/address_sorting:address_sorting": {
        "language": "c",
        "build": "all",
        "_RENAME": "address_sorting"
    },
    "@com_google_protobuf//upb:base": {
        "language": "c",
        "build": "all",
        "_RENAME": "upb_base_lib"
    },
    "@com_google_protobuf//upb:mem": {
        "language": "c",
        "build": "all",
        "_RENAME": "upb_mem_lib"
    },
    "@com_google_protobuf//upb/lex:lex": {
        "language": "c",
        "build": "all",
        "_RENAME": "upb_lex_lib",
    },
    "@com_google_protobuf//upb:message": {
        "language": "c",
        "build": "all",
        "_RENAME": "upb_message_lib"
    },
    "@com_google_protobuf//upb/json:json": {
        "language": "c",
        "build": "all",
        "_RENAME": "upb_json_lib"
    },
    "@com_google_protobuf//upb/mini_descriptor:mini_descriptor": {
        "language": "c",
        "build": "all",
        "_RENAME": "upb_mini_descriptor_lib"
    },
    "@com_google_protobuf//upb/text:text": {
        "language": "c",
        "build": "all",
        "_RENAME": "upb_textformat_lib"
    },
    "@com_google_protobuf//upb/wire:wire": {
        "language": "c",
        "build": "all",
        "_RENAME": "upb_wire_lib"
    },
    "@com_google_protobuf//third_party/utf8_range:utf8_range": {
        "language": "c",
        "build": "all",
        // rename to utf8_range_lib is necessary for now to avoid clash with utf8_range target in protobuf's cmake
        "_RENAME": "utf8_range_lib"
    },
    "@com_googlesource_code_re2//:re2": {
        "language": "c",
        "build": "all",
        "_RENAME": "re2"
    },
    "@com_google_googletest//:gtest": {
        "language": "c",
        "build": "private",
        "_RENAME": "gtest"
    },
    "@zlib//:zlib": {
        "language": "c",
        "zlib": true,
        "build": "private",
        "defaults": "zlib",
        "_RENAME": "z"
    },
    "gpr": {
        "language": "c",
        "build": "all"
    },
    "grpc": {
        "language": "c",
        "build": "all",
        "baselib": true,
        "generate_plugin_registry": true
    },
    "grpc++": {
        "language": "c++",
        "build": "all",
        "baselib": true
    },
    "grpc++_alts": {"language": "c++", "build": "all", "baselib": true},
    "grpc++_error_details": {"language": "c++", "build": "all"},
    "grpc++_reflection": {"language": "c++", "build": "all"},
    "grpc_authorization_provider": {"language": "c++", "build": "all"},
    "grpc++_unsecure": {
        "language": "c++",
        "build": "all",
        "baselib": true
    },
    "grpc_unsecure": {
        "language": "c",
        "build": "all",
        "baselib": true,
        "generate_plugin_registry": true
    },
    "grpcpp_channelz": {"language": "c++", "build": "all"},
    "grpcpp_otel_plugin": {
        "language": "c++",
        "build": "plugin"
    },
    "grpc++_test": {
        "language": "c++",
        "build": "private"
    },
    "src/compiler:grpc_plugin_support": {
        "language": "c++",
        "build": "protoc",
        "_RENAME": "grpc_plugin_support"
    },
    "src/compiler:grpc_cpp_plugin": {
        "language": "c++",
        "build": "protoc",
        "_TYPE": "target",
        "_RENAME": "grpc_cpp_plugin"
    },
    "src/compiler:grpc_csharp_plugin": {
        "language": "c++",
        "build": "protoc",
        "_TYPE": "target",
        "_RENAME": "grpc_csharp_plugin"
    },
    "src/compiler:grpc_node_plugin": {
        "language": "c++",
        "build": "protoc",
        "_TYPE": "target",
        "_RENAME": "grpc_node_plugin"
    },
    "src/compiler:grpc_objective_c_plugin": {
        "language": "c++",
        "build": "protoc",
        "_TYPE": "target",
        "_RENAME": "grpc_objective_c_plugin"
    },
    "src/compiler:grpc_php_plugin": {
        "language": "c++",
        "build": "protoc",
        "_TYPE": "target",
        "_RENAME": "grpc_php_plugin"
    },
    "src/compiler:grpc_python_plugin": {
        "language": "c++",
        "build": "protoc",
        "_TYPE": "target",
        "_RENAME": "grpc_python_plugin"
    },
    "src/compiler:grpc_ruby_plugin": {
        "language": "c++",
        "build": "protoc",
        "_TYPE": "target",
        "_RENAME": "grpc_ruby_plugin"
    },
    // TODO(jtattermusch): consider adding grpc++_core_stats
    // test support libraries
    "test/core/test_util:grpc_test_util": {
        "language": "c",
        "build": "private",
        "_RENAME": "grpc_test_util"
    },
    "test/core/test_util:grpc_test_util_unsecure": {
        "language": "c",
        "build": "private",
        "_RENAME": "grpc_test_util_unsecure"
    },
    // TODO(jtattermusch): consider adding grpc++_test_util_unsecure - it doesn't seem to be used by bazel build (don't forget to set secure: true)
    "test/cpp/util:test_config": {
        "language": "c++",
        "build": "private",
        "_RENAME": "grpc++_test_config"
    },
    "test/cpp/util:test_util": {
        "language": "c++",
        "build": "private",
        "_RENAME": "grpc++_test_util"
    },
    // benchmark support libraries
    "test/cpp/microbenchmarks:helpers": {
        "language": "c++",
        "build": "test",
        "defaults": "benchmark",
        "_RENAME": "benchmark_helpers"
    },
    "test/cpp/interop:interop_client": {
        "language": "c++",
        "build": "test",
        "run": true,
        "_TYPE": "target",
        "_RENAME": "interop_client"
    },
    "test/cpp/interop:interop_server": {
        "language": "c++",
        "build": "test",
        "run": true,
        "_TYPE": "target",
        "_RENAME": "interop_server"
    },
    // TODO(stanleycheung): re-enable this after cmake support for otel is added
    // "test/cpp/interop:xds_interop_client": {
    //     "language": "c++",
    //     "build": "test",
    //     "run": true,
    //     "_TYPE": "target",
    //     "_RENAME": "xds_interop_client",
    // },
    // "test/cpp/interop:xds_interop_server": {
    //     "language": "c++",
    //     "build": "test",
    //     "run": true,
    //     "_TYPE": "target",
    //     "_RENAME": "xds_interop_server",
    // },
    "test/cpp/interop:http2_client": {
        "language": "c++",
        "build": "test",
        "run": true,
        "_TYPE": "target",
        "_RENAME": "http2_client"
    },
    "test/cpp/qps:qps_json_driver": {
        "language": "c++",
        "build": "test",
        "run": true,
        "_TYPE": "target",
        "_RENAME": "qps_json_driver"
    },
    "test/cpp/qps:qps_worker": {
        "language": "c++",
        "build": "test",
        "run": true,
        "_TYPE": "target",
        "_RENAME": "qps_worker"
    },
    "test/cpp/util:grpc_cli": {
        "language": "c++",
        "build": "test",
        "run": true,
        "_TYPE": "target",
        "_RENAME": "grpc_cli"
    }
    // TODO(jtattermusch): create_jwt and verify_jwt breaks distribtests because it depends on grpc_test_utils and thus requires tests to be built
    // For now it's ok to disable them as these binaries aren't very useful anyway.
    // 'test/core/security:create_jwt': { 'language': 'c', 'build': 'tool', '_TYPE': 'target', '_RENAME': 'grpc_create_jwt' },
    // 'test/core/security:verify_jwt': { 'language': 'c', 'build': 'tool', '_TYPE': 'target', '_RENAME': 'grpc_verify_jwt' },
    // TODO(jtattermusch): add remaining tools such as grpc_print_google_default_creds_token (they are not used by bazel build)
    // TODO(jtattermusch): these fuzzers had no build.yaml equivalent
    // test/core/compression:message_compress_fuzzer
    // test/core/compression:message_decompress_fuzzer
    // test/core/compression:stream_compression_fuzzer
    // test/core/compression:stream_decompression_fuzzer
    // test/core/slice:b64_decode_fuzzer
    // test/core/slice:b64_encode_fuzzer
})json";

class ArtifactGen {
 public:
  void LoadRulesXml(const std::string& source) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(source.c_str());
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
            proto_src = proto_src.substr(prefix_to_strip.length());
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

  void PatchGrpcProtoLibraryRules() {
    for (auto& [name, bazel_rule] : rules_) {
      if (absl::StartsWith(name, "//") &&
          (bazel_rule.generator_function == "grpc_proto_library" ||
           bazel_rule.clazz == "cc_proto_library")) {
        bazel_rule.deps.push_back("//third_party:protobuf");
      }
    }
  }

  void PatchDescriptorUpbProtoLibrary() {
    auto it = rules_.find("@com_google_protobuf//upb:descriptor_upb_proto");
    if (it == rules_.end()) return;
    auto& bazel_rule = it->second;
    bazel_rule.srcs.push_back(
        ":src/core/ext/upb-gen/google/protobuf/descriptor.upb_minitable.c");
    bazel_rule.hdrs.push_back(
        ":src/core/ext/upb-gen/google/protobuf/descriptor.upb.h");
  }

  void PopulateCcTests() {
    for (const auto& [_, bazel_rule] : rules_) {
      if (bazel_rule.clazz != "cc_test") continue;
      std::string test_name = bazel_rule.name;
      if (!absl::StartsWith(test_name, "//")) continue;
      test_name = test_name.substr(2);
      if (!WantCcTest(test_name)) continue;
      tests_.push_back(test_name);
    }
  }

  void GenerateBuildExtraMetadataForTests() {
    for (const auto& test : tests_) {
      nlohmann::json test_dict = nlohmann::json::object();
      test_dict["build"] = "test";
      test_dict["_TYPE"] = "target";
      auto bazel_rule = *LookupRule(test);
      if (absl::c_contains(bazel_rule.tags, "manual")) {
        test_dict["run"] = false;
      }
      if (bazel_rule.flaky) {
        test_dict["run"] = false;
      }
      if (absl::c_contains(bazel_rule.tags, "no_uses_polling")) {
        test_dict["uses_polling"] = false;
      }
      if (absl::c_contains(bazel_rule.tags, "bazel_only")) {
        continue;
      }
      if (absl::StartsWith(test, "test/cpp/ext/otel")) {
        test_dict["build"] = "plugin_test";
        test_dict["plugin_option"] = "gRPC_BUILD_GRPCPP_OTEL_PLUGIN";
      }
      // if any tags that restrict platform compatibility are present,
      // generate the "platforms" field accordingly
      // TODO(jtattermusch): there is also a "no_linux" tag, but we cannot take
      // it into account as it is applied by grpc_cc_test when poller expansion
      // is made (for tests where uses_polling=true). So for now, we just
      // assume all tests are compatible with linux and ignore the "no_linux"
      // tag completely.
      if (absl::c_contains(bazel_rule.tags, "no_windows") ||
          absl::c_contains(bazel_rule.tags, "no_mac")) {
        nlohmann::json platforms = nlohmann::json::array();
        platforms.push_back("linux");
        platforms.push_back("posix");
        if (!absl::c_contains(bazel_rule.tags, "no_windows")) {
          platforms.push_back("windows");
        }
        if (!absl::c_contains(bazel_rule.tags, "no_mac")) {
          platforms.push_back("mac");
        }
        test_dict["platforms"] = platforms;
      }
      if (!bazel_rule.args.empty()) {
        test_dict["args"] = bazel_rule.args;
      }
      if (absl::StartsWith(test, "test/cpp")) {
        test_dict["language"] = "c++";
      } else if (absl::StartsWith(test, "test/core")) {
        test_dict["language"] = "c";
      } else {
        LOG(FATAL) << "wrong test: " << test;
      }
      test_metadata_[test] = test_dict;
    }
    const auto extra =
        nlohmann::json::parse(kBuildExtraMetadata, nullptr, true, true);
    for (auto it = extra.begin(); it != extra.end(); ++it) {
      test_metadata_[it.key()] = it.value();
    }
  }

  void PopulateTransitiveMetadata() {
    for (auto it = test_metadata_.begin(); it != test_metadata_.end(); ++it) {
      bazel_label_to_dep_name_[GetBazelLabel(it.key())] = it.key();
    }
    for (auto& [rule_name, rule] : rules_) {
      if (rule.transitive_deps_computed) continue;
      ComputeTransitiveMetadata(rule);
    }
  }

  void UpdateTestMetadataWithTransitiveMetadata() {
    for (auto it = test_metadata_.begin(); it != test_metadata_.end(); ++it) {
      const auto& lib_name = it.key();
      auto& lib_dict = it.value();
      if (lib_dict["build"] != "test" && lib_dict["build"] != "plugin_test") {
        continue;
      }
      if (!lib_dict.contains("_TYPE") || lib_dict["_TYPE"] != "target") {
        continue;
      }
      const auto& bazel_rule = *LookupRule(lib_name);
      if (bazel_rule.transitive_deps.count("//third_party:benchmark") > 0) {
        lib_dict["benchmark"] = true;
        lib_dict["defaults"] = "benchmark";
      }
      if (bazel_rule.transitive_deps.count("//third_party:gtest") > 0) {
        lib_dict["gtest"] = true;
        lib_dict["language"] = "c++";
      }
    }
  }

  void GenerateBuildMetadata() {
    std::vector<std::string> lib_names;
    for (auto it = test_metadata_.begin(); it != test_metadata_.end(); ++it) {
      lib_names.push_back(it.key());
    }
    for (const auto& lib_name : lib_names) {
      auto lib_dict = CreateTargetFromBazelRule(lib_name);
      lib_dict.update(test_metadata_[lib_name]);
      build_metadata_[lib_name] = lib_dict;
    }
  }

  void ConvertToBuildYamlLike() {
    std::vector<nlohmann::json> lib_list;
    std::vector<nlohmann::json> target_list;
    std::vector<nlohmann::json> test_list;
    for (auto it = build_metadata_.begin(); it != build_metadata_.end(); ++it) {
      const auto& lib_dict = it.value();
      if (!lib_dict.contains("_TYPE")) {
        lib_list.push_back(lib_dict);
      } else if (lib_dict["_TYPE"] == "library") {
        lib_list.push_back(lib_dict);
      } else if (lib_dict["_TYPE"] == "target") {
        target_list.push_back(lib_dict);
      } else if (lib_dict["_TYPE"] == "test" ||
                 lib_dict["_TYPE"] == "plugin_test") {
        test_list.push_back(lib_dict);
      }
    }
    auto scrub = [](nlohmann::json& lib,
                    std::initializer_list<std::string> explicit_fields) {
      std::vector<std::string> fields_to_remove = explicit_fields;
      for (auto it = lib.begin(); it != lib.end(); ++it) {
        if (absl::StartsWith(it.key(), "_")) {
          fields_to_remove.push_back(it.key());
        }
      }
      for (const auto& field : fields_to_remove) {
        lib.erase(field);
      }
    };
    for (auto& lib : lib_list) {
      scrub(lib, {});
    }
    for (auto& target : target_list) {
      scrub(target, {"public_headers"});
    }
    for (auto& test : test_list) {
      scrub(test, {"public_headers"});
    }
    build_yaml_like_ = {
        {"libs", lib_list},
        {"filegroups", {}},
        {"targets", target_list},
        {"tests", test_list},
    };
  }

  void GenerateExternalProtoLibraries() {
    pugi::xml_document doc;
    std::string filename = absl::GetFlag(FLAGS_external_http_archive_query);
    CHECK(!filename.empty()) << "external_http_archive_query is not set";
    pugi::xml_parse_result result = doc.load_file(filename.c_str());
    CHECK(result) << filename;

    std::vector<nlohmann::json> external_proto_libraries;
    for (const auto& query : doc.children("query")) {
      for (const auto& child : query.children("rule")) {
        if (child.attribute("class").as_string() !=
            absl::string_view("http_archive")) {
          continue;
        }
        HttpArchive http_archive;
        for (const auto& node : child.children()) {
          if (node.attribute("name").as_string() == absl::string_view("name")) {
            http_archive.name = node.attribute("value").as_string();
          } else if (node.attribute("name").as_string() ==
                     absl::string_view("urls")) {
            for (const auto& url_node : node.children()) {
              http_archive.urls.push_back(
                  url_node.attribute("value").as_string());
            }
          } else if (node.attribute("name").as_string() ==
                     absl::string_view("url")) {
            http_archive.urls.push_back(node.attribute("value").as_string());
          } else if (node.attribute("name").as_string() ==
                     absl::string_view("sha256")) {
            http_archive.sha256 = node.attribute("value").as_string();
          } else if (node.attribute("name").as_string() ==
                     absl::string_view("strip_prefix")) {
            http_archive.strip_prefix = node.attribute("value").as_string();
          }
        }
        if (external_proto_libraries_.count(http_archive.name) == 0) {
          // If this http archive is not one of the external proto libraries,
          // we don't want to include it as a CMake target
          continue;
        }

        const auto& extlib =
            external_proto_libraries_.find(http_archive.name)->second;
        auto lib = nlohmann::json{
            {"destination", extlib.destination},
            {"proto_prefix", extlib.proto_prefix},
            {"urls", http_archive.urls},
            {"hash", http_archive.sha256},
            {"strip_prefix", http_archive.strip_prefix},
        };
        external_proto_libraries.push_back(lib);
      }
    }
    build_yaml_like_["external_proto_libraries"] = external_proto_libraries;
  }

  nlohmann::json Result() { return build_yaml_like_; }

 private:
  // Computes the final build metadata for Bazel target with rule_name.
  //
  // The dependencies that will appear on the deps list are:
  //
  // * Public build targets including binaries and tests;
  // * External targets, like absl, re2.
  //
  // All other intermediate dependencies will be merged, which means their
  // source file, headers, etc. will be collected into one build target. This
  // step of processing will greatly reduce the complexity of the generated
  // build specifications for other build systems, like CMake, Make, setuptools.
  //
  // The final build metadata are:
  // * _TRANSITIVE_DEPS: all the transitive dependencies including intermediate
  //                     targets;
  // * _COLLAPSED_DEPS:  dependencies that fits our requirement above, and it
  //                     will remove duplicated items and produce the shortest
  //                     possible dependency list in alphabetical order;
  // * _COLLAPSED_SRCS:  the merged source files;
  // * _COLLAPSED_PUBLIC_HEADERS: the merged public headers;
  // * _COLLAPSED_HEADERS: the merged non-public headers;
  // * _EXCLUDE_DEPS: intermediate targets to exclude when performing collapsing
  //      of sources and dependencies.
  //
  // For the collapsed_deps, the algorithm improved cases like:
  //
  // The result in the past:
  //     end2end_tests -> [grpc_test_util, grpc, gpr, address_sorting, upb]
  //     grpc_test_util -> [grpc, gpr, address_sorting, upb, ...]
  //     grpc -> [gpr, address_sorting, upb, ...]
  //
  // The result of the algorithm:
  //     end2end_tests -> [grpc_test_util]
  //     grpc_test_util -> [grpc]
  //     grpc -> [gpr, address_sorting, upb, ...]
  void ComputeTransitiveMetadata(BazelRule& bazel_rule) {
    auto direct_deps = ExtractDeps(bazel_rule);
    std::set<std::string> transitive_deps;
    std::set<std::string> collapsed_deps;
    std::set<std::string> exclude_deps;
    std::set<std::string> collapsed_srcs = ExtractSources(bazel_rule);
    std::set<std::string> collapsed_public_headers =
        ExtractPublicHeaders(bazel_rule);
    std::set<std::string> collapsed_headers =
        ExtractNonPublicHeaders(bazel_rule);

    auto update = [](const std::set<std::string>& add,
                     std::set<std::string>& to) {
      for (const auto& a : add) to.insert(a);
    };

    for (const auto& dep : direct_deps) {
      auto external_dep_name_maybe = ExternalDepNameFromBazelDependency(dep);

      auto it = rules_.find(dep);
      if (it != rules_.end()) {
        BazelRule& dep_rule = it->second;
        // Descend recursively, but no need to do that for external deps
        if (!external_dep_name_maybe.has_value()) {
          if (!dep_rule.transitive_deps_computed) {
            ComputeTransitiveMetadata(dep_rule);
          }
          update(dep_rule.transitive_deps, transitive_deps);
          update(dep_rule.collapsed_deps, collapsed_deps);
          update(dep_rule.exclude_deps, exclude_deps);
        }
      }
      // This dep is a public target, add it as a dependency
      auto it_bzl = bazel_label_to_dep_name_.find(dep);
      if (it_bzl != bazel_label_to_dep_name_.end()) {
        transitive_deps.insert(it_bzl->second);
        collapsed_deps.insert(it_bzl->second);
        // Add all the transitive deps of our every public dep to exclude
        // list since we want to avoid building sources that are already
        // built by our dependencies
        update(rules_[dep].transitive_deps, exclude_deps);
        continue;
      }
      // This dep is an external target, add it as a dependency
      if (external_dep_name_maybe.has_value()) {
        transitive_deps.insert(external_dep_name_maybe.value());
        collapsed_deps.insert(external_dep_name_maybe.value());
        continue;
      }
    }
    // Direct dependencies are part of transitive dependencies
    update(direct_deps, transitive_deps);
    // Calculate transitive public deps (needed for collapsing sources)
    std::set<std::string> transitive_public_deps;
    for (const auto& dep : transitive_deps) {
      if (bazel_label_to_dep_name_.count(dep) > 0) {
        transitive_public_deps.insert(dep);
      }
    }
    // Remove intermediate targets that our public dependencies already depend
    // on. This is the step that further shorten the deps list.
    std::set<std::string> new_collapsed_deps;
    for (const auto& dep : collapsed_deps) {
      if (exclude_deps.count(dep) == 0) {
        new_collapsed_deps.insert(dep);
      }
    }
    collapsed_deps.swap(new_collapsed_deps);
    // Compute the final source files and headers for this build target whose
    // name is `rule_name` (input argument of this function).
    //
    // Imaging a public target PX has transitive deps [IA, IB, PY, IC, PZ]. PX,
    // PY and PZ are public build targets. And IA, IB, IC are intermediate
    // targets. In addition, PY depends on IC.
    //
    // Translate the condition into dependency graph:
    //   PX -> [IA, IB, PY, IC, PZ]
    //   PY -> [IC]
    //   Public targets: [PX, PY, PZ]
    //
    // The collapsed dependencies of PX: [PY, PZ].
    // The excluded dependencies of X: [PY, IC, PZ].
    // (IC is excluded as a dependency of PX. It is already included in PY,
    // hence it would be redundant to include it again.)
    //
    // Target PX should include source files and headers of [PX, IA, IB] as
    // final build metadata.
    for (const auto& dep : transitive_deps) {
      if (exclude_deps.count(dep) == 0 &&
          transitive_public_deps.count(dep) == 0) {
        if (rules_.count(dep) != 0) {
          update(ExtractSources(rules_[dep]), collapsed_srcs);
          update(ExtractPublicHeaders(rules_[dep]), collapsed_public_headers);
          update(ExtractNonPublicHeaders(rules_[dep]), collapsed_headers);
        }
      }
    }
    bazel_rule.transitive_deps_computed = true;
    bazel_rule.transitive_deps = std::move(transitive_deps);
    bazel_rule.collapsed_deps = std::move(collapsed_deps);
    bazel_rule.exclude_deps = std::move(exclude_deps);
    bazel_rule.collapsed_srcs = std::move(collapsed_srcs);
    bazel_rule.collapsed_public_headers = std::move(collapsed_public_headers);
    bazel_rule.collapsed_headers = std::move(collapsed_headers);
  }

  // Returns name of dependency if external bazel dependency is provided or
  // nullopt
  std::optional<std::string> ExternalDepNameFromBazelDependency(
      std::string bazel_dep) {
    if (absl::StartsWith(bazel_dep, "@com_google_absl//")) {
      return bazel_dep.substr(strlen("@com_google_absl//"));
    }
    if (bazel_dep == "@com_github_google_benchmark//:benchmark") {
      return "benchmark";
    }
    if (bazel_dep == "@boringssl//:ssl") {
      return "libssl";
    }
    if (bazel_dep == "@com_github_cares_cares//:ares") {
      return "cares";
    }
    if (bazel_dep == "@com_google_protobuf//:protobuf" ||
        bazel_dep == "@com_google_protobuf//:protobuf_headers") {
      return "protobuf";
    }
    if (bazel_dep == "@com_google_protobuf//:protoc_lib") {
      return "protoc";
    }
    if (bazel_dep == "@io_opentelemetry_cpp//api:api") {
      return "opentelemetry-cpp::api";
    }
    if (bazel_dep == "@io_opentelemetry_cpp//sdk/src/metrics:metrics") {
      return "opentelemetry-cpp::metrics";
    }
    // Two options here:
    // * either this is not external dependency at all (which is fine, we will
    //   treat it as internal library)
    // * this is external dependency, but we don't want to make the dependency
    //   explicit in the build metadata for other build systems.
    return std::nullopt;
  }

  std::set<std::string> ExtractDeps(const BazelRule& bazel_rule) {
    std::set<std::string> deps;
    for (const auto& dep : bazel_rule.deps) {
      deps.insert(dep);
    }
    for (const auto& src : bazel_rule.srcs) {
      if (!absl::EndsWith(src, ".cc") && !absl::EndsWith(src, ".c") &&
          !absl::EndsWith(src, ".proto")) {
        auto rule_it = rules_.find(src);
        if (rule_it != rules_.end()) {
          // This label doesn't point to a source file, but another Bazel
          // target. This is required for :pkg_cc_proto_validate targets,
          // and it's generally allowed by Bazel.
          deps.insert(src);
        }
      }
    }
    return deps;
  }

  std::set<std::string> ExtractSources(const BazelRule& bazel_rule) {
    std::set<std::string> srcs;
    for (const auto& src : bazel_rule.srcs) {
      if (absl::StartsWith(src, "@com_google_protobuf//") &&
          absl::EndsWith(src, ".proto")) {
        continue;
      }
      if (absl::EndsWith(src, ".cc") || absl::EndsWith(src, ".c") ||
          absl::EndsWith(src, ".proto")) {
        auto source_file_maybe = TryExtractSourceFilePath(src);
        if (source_file_maybe.has_value()) {
          srcs.insert(source_file_maybe.value());
        }
      }
    }
    return srcs;
  }

  std::set<std::string> ExtractPublicHeaders(const BazelRule& bazel_rule) {
    std::set<std::string> headers;
    for (const auto& hdr : bazel_rule.hdrs) {
      if (absl::StartsWith(hdr, "//:include/") && HasHeaderSuffix(hdr)) {
        auto source_file_maybe = TryExtractSourceFilePath(hdr);
        if (source_file_maybe.has_value()) {
          headers.insert(source_file_maybe.value());
        }
      }
    }
    return headers;
  }

  std::set<std::string> ExtractNonPublicHeaders(const BazelRule& bazel_rule) {
    std::set<std::string> headers;
    for (const auto* vec :
         {&bazel_rule.hdrs, &bazel_rule.textual_hdrs, &bazel_rule.srcs}) {
      for (const auto& hdr : *vec) {
        if (!absl::StartsWith(hdr, "//:include/") && HasHeaderSuffix(hdr)) {
          auto source_file_maybe = TryExtractSourceFilePath(hdr);
          if (source_file_maybe.has_value()) {
            headers.insert(source_file_maybe.value());
          }
        }
      }
    }
    return headers;
  }

  bool HasHeaderSuffix(absl::string_view hdr) {
    return absl::EndsWith(hdr, ".h") || absl::EndsWith(hdr, ".hpp") ||
           absl::EndsWith(hdr, ".inc");
  }

  bool WantCcTest(absl::string_view test) {
    // most qps tests are autogenerated, we are fine without them
    if (absl::StartsWith(test, "test/cpp/qps:")) return false;
    // microbenchmarks aren't needed for checking correctness
    if (absl::StartsWith(test, "test/cpp/microbenchmarks:")) return false;
    if (absl::StartsWith(test, "test/core/promise/benchmark:")) return false;
    // we have trouble with census dependency outside of bazel
    if (absl::StartsWith(test, "test/cpp/ext/filters/census:")) return false;
    if (absl::StartsWith(test,
                         "test/core/server:xds_channel_stack_modifier_test")) {
      return false;
    }
    if (absl::StartsWith(test, "test/cpp/ext/gcp:")) return false;
    if (absl::StartsWith(test, "test/cpp/ext/filters/logging:")) return false;
    if (absl::StartsWith(test, "test/cpp/interop:observability_interop")) {
      return false;
    }
    // we have not added otel dependency outside of bazel
    if (absl::StartsWith(test, "test/cpp/ext/csm:")) return false;
    if (absl::StartsWith(test, "test/cpp/interop:xds_interop")) return false;
    // missing opencensus/stats/stats.h
    if (absl::StartsWith(
            test, "test/cpp/end2end:server_load_reporting_end2end_test")) {
      return false;
    }
    if (absl::StartsWith(
            test, "test/cpp/server/load_reporter:lb_load_reporter_test")) {
      return false;
    }
    // The test uses --running_under_bazel cmdline argument
    // To avoid the trouble needing to adjust it, we just skip the test
    if (absl::StartsWith(
            test, "test/cpp/naming:resolver_component_tests_runner_invoker")) {
      return false;
    }
    // the test requires 'client_crash_test_server' to be built
    if (absl::StartsWith(test, "test/cpp/end2end:time_change_test")) {
      return false;
    }
    if (absl::StartsWith(test, "test/cpp/end2end:client_crash_test")) {
      return false;
    }
    // the test requires 'server_crash_test_client' to be built
    if (absl::StartsWith(test, "test/cpp/end2end:server_crash_test")) {
      return false;
    }
    // test never existed under build.yaml and it fails -> skip it
    if (absl::StartsWith(test, "test/core/tsi:ssl_session_cache_test")) {
      return false;
    }
    // the binary of this test does not get built with cmake
    if (absl::StartsWith(test, "test/cpp/util:channelz_sampler_test")) {
      return false;
    }
    // chaotic good not supported outside bazel
    if (absl::StartsWith(test, "test/core/transport/chaotic_good")) {
      return false;
    }
    // we don't need to generate fuzzers outside of bazel
    if (absl::EndsWith(test, "_fuzzer")) return false;
    if (absl::StrContains(test, "_fuzzer_")) return false;
    return true;
  }

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
              label, {{lib_name, prefix}, {":", "/"}, {"//", "/"}});
        }
      }
      // This source file is external, and we need to translate the
      // @REPO_NAME to a valid path prefix. At this stage, we need
      // to check repo name, since the label/path mapping is not
      // available in BUILD files.
      for (const auto& [lib_name, external_proto_lib] :
           external_proto_libraries_) {
        if (absl::StartsWith(label, "@" + lib_name + "//")) {
          return absl::StrReplaceAll(label, {{absl::StrCat("@", lib_name, "//"),
                                              external_proto_lib.proto_prefix},
                                             {":", "/"}});
        }
      }
      // No external library match found.
      return std::nullopt;
    }
    if (absl::StartsWith(label, "//")) label = label.substr(2);
    if (absl::StartsWith(label, ":")) label = label.substr(1);
    return absl::StrReplaceAll(label, {{":", "/"}});
  }

  static std::string GetBazelLabel(std::string target_name) {
    if (absl::StartsWith(target_name, "@")) return target_name;
    if (absl::StrContains(target_name, ":")) {
      return absl::StrCat("//", target_name);
    } else {
      return absl::StrCat("//:", target_name);
    }
  }

  BazelRule* LookupRule(std::string target_name) {
    auto it = rules_.find(GetBazelLabel(target_name));
    if (it == rules_.end()) {
      LOG(ERROR) << "Rule not found: " << target_name
                 << " bazel label: " << GetBazelLabel(target_name);
      return nullptr;
    }
    return &it->second;
  }

  nlohmann::json CreateTargetFromBazelRule(std::string target_name) {
    auto bazel_rule = *LookupRule(target_name);
    return nlohmann::json{
        {"name", target_name},
        {"_PUBLIC_HEADERS_BAZEL", ExtractPublicHeaders(bazel_rule)},
        {"_HEADERS_BAZEL", ExtractNonPublicHeaders(bazel_rule)},
        {"_SRC_BAZEL", ExtractSources(bazel_rule)},
        {"_DEPS_BAZEL", ExtractDeps(bazel_rule)},
        {"public_headers", bazel_rule.collapsed_public_headers},
        {"headers", bazel_rule.collapsed_headers},
        {"src", bazel_rule.collapsed_srcs},
        {"deps", bazel_rule.collapsed_deps},
        {"transitive_deps", bazel_rule.transitive_deps},
        {"exclude_deps", bazel_rule.exclude_deps},
        {"collapsed_deps", bazel_rule.collapsed_deps},
        {"collapsed_headers", bazel_rule.collapsed_headers},
        {"collapsed_srcs", bazel_rule.collapsed_srcs},
    };
  }

  std::map<std::string, BazelRule> rules_;
  std::vector<std::string> tests_;
  nlohmann::json test_metadata_ = nlohmann::json::object();
  nlohmann::json build_metadata_ = nlohmann::json::object();
  nlohmann::json build_yaml_like_ = nlohmann::json::object();
  std::map<std::string, std::string> bazel_label_to_dep_name_;

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

nlohmann::json ExtractMetadataFromBazelXml() {
  ArtifactGen generator;
  for (auto target_query : absl::GetFlag(FLAGS_target_query)) {
    generator.LoadRulesXml(target_query);
  }
  generator.ExpandUpbProtoLibraryRules();
  generator.PatchGrpcProtoLibraryRules();
  generator.PatchDescriptorUpbProtoLibrary();
  generator.PopulateCcTests();
  generator.GenerateBuildExtraMetadataForTests();
  generator.PopulateTransitiveMetadata();
  generator.UpdateTestMetadataWithTransitiveMetadata();
  generator.GenerateBuildMetadata();
  generator.ConvertToBuildYamlLike();
  generator.GenerateExternalProtoLibraries();
  return generator.Result();
}
