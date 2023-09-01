# Bazel Support

## Basic Usage

The `grpc/grpc` repository's primary build system is Bazel. Rules are provided
for C++, Python, and Objective-C. While C++ supports other build systems such as
CMake, these rules are actually generated from the Bazel definitions.

Projects built with Bazel may use the `grpc/grpc` repo not only to add a
dependency on the library itself, but also to generate protobuf, stub, and
servicer code. To do so, one must invoke the `grpc_deps` and `grpc_extra_deps`
repository rules in their `WORKSPACE` file:

```starlark
workspace(name = "example_workspace")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "com_github_grpc_grpc",
    strip_prefix = "grpc-1.45.0",
    sha256 = "ec19657a677d49af59aa806ec299c070c882986c9fcc022b1c22c2a3caf01bcd",
    urls = ["https://github.com/grpc/grpc/archive/refs/tags/v1.45.0.tar.gz"],
)

load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")

grpc_deps()

load("@com_github_grpc_grpc//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")

grpc_extra_deps()
```

## Supported Versions

In general, gRPC supports building with the latest patch release of the two most
recent LTS versions of Bazel. However individual releases may have a broader
compatibility range. The currently supported versions are captured by the
following list:

- [`6.3.2`](https://github.com/bazelbuild/bazel/releases/tag/6.3.2)
- [`5.4.1`](https://github.com/bazelbuild/bazel/releases/tag/5.4.1)
