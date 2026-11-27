# Bazel Support

## Basic Usage

The `grpc/grpc` repository's primary build system is Bazel. Rules are provided
for C++, Python, and Objective-C. While C++ supports other build systems such as
CMake, these rules are actually generated from the Bazel definitions.

Projects built with Bazel may use the `grpc/grpc` repo not only to add a
dependency on the library itself, but also to generate protobuf, stub, and
servicer code. To do so, declare the dependency in your `MODULE.bazel` file:

```starlark
bazel_dep(name = "grpc", version = "1.71.0", repo_name = "com_github_grpc_grpc")
```

## Supported Versions

gRPC supports building with the latest stable release of Bazel,
as well as the previous major version release for at least 6 months
after it transitions into maintenance mode.
This is consistent with the supported build systems of
[the Google Foundational C++ Support Policy](https://opensource.google/documentation/policies/cplusplus-support).
However individual releases may have a broader
compatibility range. The currently supported versions are captured by the
following list:

- [`8.7.0`](https://github.com/bazelbuild/bazel/releases/tag/8.7.0)
