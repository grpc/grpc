#include "build_metadata.h"

#include <map>
#include <string>

#include "include/nlohmann/json.hpp"

namespace grpc_tools {
namespace artifact_gen {

static const char* kBuildExtraMetadata = R"json({
    "//third_party/address_sorting:address_sorting": {
        "language": "c",
        "build": "all",
        "_RENAME": "address_sorting"
    },
    "@com_google_protobuf//upb/base:base": {
        "language": "c",
        "build": "all",
        "_RENAME": "upb_base_lib"
    },
    "@com_google_protobuf//upb/hash:hash": {
        "language": "c",
        "build": "all",
        "_RENAME": "upb_hash_lib"
    },
    "@com_google_protobuf//upb/mem:mem": {
        "language": "c",
        "build": "all",
        "_RENAME": "upb_mem_lib"
    },
    "@com_google_protobuf//upb/lex:lex": {
        "language": "c",
        "build": "all",
        "_RENAME": "upb_lex_lib"
    },
    "@com_google_protobuf//upb/message:message": {
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
    "@com_google_protobuf//upb/mini_table:mini_table": {
        "language": "c",
        "build": "all",
        "_RENAME": "upb_mini_table_lib"
    },
    "@com_google_protobuf//upb/reflection:reflection": {
        "language": "c",
        "build": "all",
        "_RENAME": "upb_reflection_lib"
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
})json";

nlohmann::json GetBuildExtraMetadata() {
  return nlohmann::json::parse(kBuildExtraMetadata, nullptr, true, true);
}

std::map<std::string, std::string> GetBazelLabelToRenamedMapping() {
  std::map<std::string, std::string> mapping;
  auto metadata = GetBuildExtraMetadata();
  
  for (auto it = metadata.begin(); it != metadata.end(); ++it) {
    const std::string& bazel_label = it.key();
    const auto& lib_metadata = it.value();
    if (lib_metadata.contains("_RENAME")) {
      mapping[bazel_label] = lib_metadata["_RENAME"];
    }
  }
  
  return mapping;
}

}  // namespace artifact_gen
}  // namespace grpc_tools 