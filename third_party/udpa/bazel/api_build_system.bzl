load("@com_envoyproxy_protoc_gen_validate//bazel:pgv_proto_library.bzl", "pgv_cc_proto_library")
load("@com_google_protobuf//:protobuf.bzl", _py_proto_library = "py_proto_library")
load("@io_bazel_rules_go//go:def.bzl", "go_test")
load("@io_bazel_rules_go//proto:def.bzl", "go_grpc_library", "go_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")

_PY_PROTO_SUFFIX = "_py_proto"
_CC_PROTO_SUFFIX = "_cc_proto"
_CC_GRPC_SUFFIX = "_cc_grpc"
_GO_PROTO_SUFFIX = "_go_proto"
_GO_IMPORTPATH_PREFIX = "github.com/cncf/udpa/go/"

_COMMON_PROTO_DEPS = [
    "@com_google_protobuf//:any_proto",
    "@com_google_protobuf//:descriptor_proto",
    "@com_google_protobuf//:duration_proto",
    "@com_google_protobuf//:empty_proto",
    "@com_google_protobuf//:struct_proto",
    "@com_google_protobuf//:timestamp_proto",
    "@com_google_protobuf//:wrappers_proto",
    "@com_google_googleapis//google/api:http_proto",
    "@com_google_googleapis//google/api:annotations_proto",
    "@com_google_googleapis//google/rpc:status_proto",
    "@com_envoyproxy_protoc_gen_validate//validate:validate_proto",
]

def _proto_mapping(dep, proto_suffix):
    prefix = "@" + Label(dep).workspace_name if not dep.startswith("//") else ""
    return prefix + "//" + Label(dep).package + ":" + Label(dep).name + proto_suffix

def _go_proto_mapping(dep):
    return _proto_mapping(dep, _GO_PROTO_SUFFIX)

def _cc_proto_mapping(dep):
    return _proto_mapping(dep, _CC_PROTO_SUFFIX)

def _py_proto_mapping(dep):
    return _proto_mapping(dep, _PY_PROTO_SUFFIX)

# TODO(htuch): Convert this to native py_proto_library once
# https://github.com/bazelbuild/bazel/issues/3935 and/or
# https://github.com/bazelbuild/bazel/issues/2626 are resolved.
def _udpa_py_proto_library(name, srcs = [], deps = []):
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
def py_proto_library(name, deps = []):
    srcs = [dep[:-6] + ".proto" if dep.endswith("_proto") else dep for dep in deps]
    proto_deps = []

    # py_proto_library in googleapis specifies *_proto rules in dependencies.
    # By rewriting *_proto to *.proto above, the dependencies in *_proto rules are not preserved.
    # As a workaround, manually specify the proto dependencies for the imported python rules.
    if name == "annotations_py_proto":
        proto_deps = proto_deps + [":http_py_proto"]
    _py_proto_library(
        name = name,
        srcs = srcs,
        default_runtime = "@com_google_protobuf//:protobuf_python",
        protoc = "@com_google_protobuf//:protoc",
        deps = proto_deps + ["@com_google_protobuf//:protobuf_python"],
        visibility = ["//visibility:public"],
    )

def _udpa_cc_py_proto_library(
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
    _udpa_py_proto_library(name, srcs, deps)

    # Optionally define gRPC services
    if has_services:
        # TODO: neither C++ or Python service generation is supported today, follow the Envoy example to implementthis.
        pass

def udpa_proto_package(srcs = [], deps = [], has_services = False, visibility = ["//visibility:public"]):
    if srcs == []:
        srcs = native.glob(["*.proto"])

    name = "pkg"
    _udpa_cc_py_proto_library(
        name = name,
        visibility = visibility,
        srcs = srcs,
        deps = deps,
        has_services = has_services,
    )

    compilers = ["@io_bazel_rules_go//proto:go_proto", "//bazel:pgv_plugin_go"]
    if has_services:
        compilers = ["@io_bazel_rules_go//proto:go_grpc", "//bazel:pgv_plugin_go"]

    go_proto_library(
        name = name + _GO_PROTO_SUFFIX,
        compilers = compilers,
        importpath = _GO_IMPORTPATH_PREFIX + native.package_name(),
        proto = name,
        visibility = ["//visibility:public"],
        deps = [_go_proto_mapping(dep) for dep in deps] + [
            "@com_envoyproxy_protoc_gen_validate//validate:go_default_library",
            "@com_github_golang_protobuf//ptypes:go_default_library_gen",
            "@go_googleapis//google/api:annotations_go_proto",
            "@go_googleapis//google/rpc:status_go_proto",
            "@io_bazel_rules_go//proto/wkt:any_go_proto",
            "@io_bazel_rules_go//proto/wkt:duration_go_proto",
            "@io_bazel_rules_go//proto/wkt:struct_go_proto",
            "@io_bazel_rules_go//proto/wkt:timestamp_go_proto",
            "@io_bazel_rules_go//proto/wkt:wrappers_go_proto",
        ],
    )

def udpa_cc_test(name, **kwargs):
    native.cc_test(
        name = name,
        **kwargs
    )

def udpa_go_test(name, **kwargs):
    go_test(
        name = name,
        **kwargs
    )
