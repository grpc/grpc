load("@rules_cc//cc:defs.bzl", "cc_test")
load("@com_envoyproxy_protoc_gen_validate//bazel:pgv_proto_library.bzl", "pgv_cc_proto_library")
load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_google_protobuf//:protobuf.bzl", _py_proto_library = "py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")
load("@io_bazel_rules_go//go:def.bzl", "go_test")
load("@rules_proto//proto:defs.bzl", "proto_library")
load(
    "//bazel:external_proto_deps.bzl",
    "EXTERNAL_PROTO_CC_BAZEL_DEP_MAP",
    "EXTERNAL_PROTO_GO_BAZEL_DEP_MAP",
    "EXTERNAL_PROTO_PY_BAZEL_DEP_MAP",
)

_PY_PROTO_SUFFIX = "_py_proto"
_CC_PROTO_SUFFIX = "_cc_proto"
_CC_GRPC_SUFFIX = "_cc_grpc"
_GO_PROTO_SUFFIX = "_go_proto"
_GO_IMPORTPATH_PREFIX = "github.com/envoyproxy/go-control-plane/"

_COMMON_PROTO_DEPS = [
    "@com_google_protobuf//:any_proto",
    "@com_google_protobuf//:descriptor_proto",
    "@com_google_protobuf//:duration_proto",
    "@com_google_protobuf//:empty_proto",
    "@com_google_protobuf//:struct_proto",
    "@com_google_protobuf//:timestamp_proto",
    "@com_google_protobuf//:wrappers_proto",
    "@com_google_googleapis//google/api:http_proto",
    "@com_google_googleapis//google/api:httpbody_proto",
    "@com_google_googleapis//google/api:annotations_proto",
    "@com_google_googleapis//google/rpc:status_proto",
    "@com_envoyproxy_protoc_gen_validate//validate:validate_proto",
]

def _proto_mapping(dep, proto_dep_map, proto_suffix):
    mapped = proto_dep_map.get(dep)
    if mapped == None:
        prefix = "@" + Label(dep).workspace_name if not dep.startswith("//") else ""
        return prefix + "//" + Label(dep).package + ":" + Label(dep).name + proto_suffix
    return mapped

def _go_proto_mapping(dep):
    return _proto_mapping(dep, EXTERNAL_PROTO_GO_BAZEL_DEP_MAP, _GO_PROTO_SUFFIX)

def _cc_proto_mapping(dep):
    return _proto_mapping(dep, EXTERNAL_PROTO_CC_BAZEL_DEP_MAP, _CC_PROTO_SUFFIX)

def _py_proto_mapping(dep):
    return _proto_mapping(dep, EXTERNAL_PROTO_PY_BAZEL_DEP_MAP, _PY_PROTO_SUFFIX)

# TODO(htuch): Convert this to native py_proto_library once
# https://github.com/bazelbuild/bazel/issues/3935 and/or
# https://github.com/bazelbuild/bazel/issues/2626 are resolved.
def _api_py_proto_library(name, srcs = [], deps = []):
    _py_proto_library(
        name = name + _PY_PROTO_SUFFIX,
        srcs = srcs,
        default_runtime = "@com_google_protobuf//:protobuf_python",
        protoc = "@com_google_protobuf//:protoc",
        deps = [_py_proto_mapping(dep) for dep in deps] + [
            "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
            "@com_google_googleapis//google/rpc:status_py_proto",
            "@com_google_googleapis//google/api:annotations_py_proto",
            "@com_google_googleapis//google/api:http_py_proto",
            "@com_google_googleapis//google/api:httpbody_py_proto",
        ],
        visibility = ["//visibility:public"],
    )

# This defines googleapis py_proto_library. The repository does not provide its definition and requires
# overriding it in the consuming project (see https://github.com/grpc/grpc/issues/19255 for more details).
def py_proto_library(name, deps = [], plugin = None):
    srcs = [dep[:-6] + ".proto" if dep.endswith("_proto") else dep for dep in deps]
    proto_deps = []

    # py_proto_library in googleapis specifies *_proto rules in dependencies.
    # By rewriting *_proto to *.proto above, the dependencies in *_proto rules are not preserved.
    # As a workaround, manually specify the proto dependencies for the imported python rules.
    if name == "annotations_py_proto":
        proto_deps = proto_deps + [":http_py_proto"]

    # checked.proto depends on syntax.proto, we have to add this dependency manually as well.
    if name == "checked_py_proto":
        proto_deps = proto_deps + [":syntax_py_proto"]

    # py_proto_library does not support plugin as an argument yet at gRPC v1.25.0:
    # https://github.com/grpc/grpc/blob/v1.25.0/bazel/python_rules.bzl#L72.
    # plugin should also be passed in here when gRPC version is greater than v1.25.x.
    _py_proto_library(
        name = name,
        srcs = srcs,
        default_runtime = "@com_google_protobuf//:protobuf_python",
        protoc = "@com_google_protobuf//:protoc",
        deps = proto_deps + ["@com_google_protobuf//:protobuf_python"],
        visibility = ["//visibility:public"],
    )

def _api_cc_grpc_library(name, proto, deps = []):
    cc_grpc_library(
        name = name,
        srcs = [proto],
        deps = deps,
        proto_only = False,
        grpc_only = True,
        visibility = ["//visibility:public"],
    )

def api_cc_py_proto_library(
        name,
        visibility = ["//visibility:private"],
        srcs = [],
        deps = [],
        linkstatic = 0,
        has_services = 0):
    relative_name = ":" + name
    proto_library(
        name = name,
        srcs = srcs,
        deps = deps + _COMMON_PROTO_DEPS,
        visibility = visibility,
    )
    cc_proto_library_name = name + _CC_PROTO_SUFFIX
    pgv_cc_proto_library(
        name = cc_proto_library_name,
        linkstatic = linkstatic,
        cc_deps = [_cc_proto_mapping(dep) for dep in deps] + [
            "@com_google_googleapis//google/api:http_cc_proto",
            "@com_google_googleapis//google/api:httpbody_cc_proto",
            "@com_google_googleapis//google/api:annotations_cc_proto",
            "@com_google_googleapis//google/rpc:status_cc_proto",
        ],
        deps = [relative_name],
        visibility = ["//visibility:public"],
    )
    _api_py_proto_library(name, srcs, deps)

    # Optionally define gRPC services
    if has_services:
        # TODO: when Python services are required, add to the below stub generations.
        cc_grpc_name = name + _CC_GRPC_SUFFIX
        cc_proto_deps = [cc_proto_library_name] + [_cc_proto_mapping(dep) for dep in deps]
        _api_cc_grpc_library(name = cc_grpc_name, proto = relative_name, deps = cc_proto_deps)

def api_cc_test(name, **kwargs):
    cc_test(
        name = name,
        **kwargs
    )

def api_go_test(name, **kwargs):
    go_test(
        name = name,
        **kwargs
    )

def api_proto_package(
        name = "pkg",
        srcs = [],
        deps = [],
        has_services = False,
        visibility = ["//visibility:public"]):
    if srcs == []:
        srcs = native.glob(["*.proto"])

    name = "pkg"
    api_cc_py_proto_library(
        name = name,
        visibility = visibility,
        srcs = srcs,
        deps = deps,
        has_services = has_services,
    )

    compilers = ["@io_bazel_rules_go//proto:go_proto", "@envoy_api//bazel:pgv_plugin_go"]
    if has_services:
        compilers = ["@io_bazel_rules_go//proto:go_grpc", "@envoy_api//bazel:pgv_plugin_go"]

    # Because RBAC proro depends on googleapis syntax.proto and checked.proto,
    # which share the same go proto library, it causes duplicative dependencies.
    # Thus, we use depset().to_list() to remove duplicated depenencies.
    go_proto_library(
        name = name + _GO_PROTO_SUFFIX,
        compilers = compilers,
        importpath = _GO_IMPORTPATH_PREFIX + native.package_name(),
        proto = name,
        visibility = ["//visibility:public"],
        deps = depset([_go_proto_mapping(dep) for dep in deps] + [
            "@com_envoyproxy_protoc_gen_validate//validate:go_default_library",
            "@com_github_golang_protobuf//ptypes:go_default_library_gen",
            "@go_googleapis//google/api:annotations_go_proto",
            "@go_googleapis//google/rpc:status_go_proto",
            "@io_bazel_rules_go//proto/wkt:any_go_proto",
            "@io_bazel_rules_go//proto/wkt:duration_go_proto",
            "@io_bazel_rules_go//proto/wkt:struct_go_proto",
            "@io_bazel_rules_go//proto/wkt:timestamp_go_proto",
            "@io_bazel_rules_go//proto/wkt:wrappers_go_proto",
        ]).to_list(),
    )
