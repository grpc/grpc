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

#include "absl/algorithm/container.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "include/inja/inja.hpp"
#include "include/nlohmann/json.hpp"
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
                 bazel_rule.flaky ? "true" : "true",
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
    },
    "test/cpp/ext/otel:otel_plugin_test": {
        "language": "c++",
        "build": "plugin_test",
        "_TYPE": "target",
        "plugin_option": "gRPC_BUILD_GRPCPP_OTEL_PLUGIN",
        "_RENAME": "otel_plugin_test"
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
    std::map<std::string, nlohmann::json> test_metadata;
    for (const auto& test : tests_) {
      nlohmann::json test_dict = nlohmann::json::object();
      test_dict["build"] = "test";
      test_dict["_TYPE"] = "target";
      auto bazel_rule = LookupRule(test).value();
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
      auto test_name = absl::StrReplaceAll(test, {{"/", "_"}, {":", "_"}});
      test_metadata[test_name] = test_dict;
    }
    const auto extra =
        nlohmann::json::parse(kBuildExtraMetadata, nullptr, true, true);
    LOG(INFO) << extra;
    for (nlohmann::json::const_iterator it = extra.begin(); it != extra.end();
         ++it) {
      test_metadata[it.key()] = it.value();
    }
  }

 private:
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

  static std::string GetBazelLabel(std::string target_name) {
    if (absl::StartsWith(target_name, "@")) return target_name;
    if (absl::StrContains(target_name, ":")) {
      return absl::StrCat("//", target_name);
    } else {
      return absl::StrCat("//:", target_name);
    }
  }

  std::optional<BazelRule> LookupRule(std::string target_name) {
    auto it = rules_.find(GetBazelLabel(target_name));
    if (it == rules_.end()) return std::nullopt;
    return it->second;
  }

  std::map<std::string, BazelRule> rules_;
  std::vector<std::string> tests_;
  nlohmann::json metadata_ = nlohmann::json::object();

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
  generator.PatchGrpcProtoLibraryRules();
  generator.PatchDescriptorUpbProtoLibrary();
  generator.PopulateCcTests();
  generator.GenerateBuildExtraMetadataForTests();
  return 0;
}
