
# Overview

A C++ implementation of gRPC

# To start using gRPC C++

In the C++ world, there's no universally accepted standard for managing project dependencies.
Therefore, gRPC supports several major build systems, which should satisfy most users.

## bazel

We recommend using Bazel for projects that use gRPC as it will give you the best developer experience
(easy handling of dependencies that support bazel & fast builds).

To add gRPC as a dependency in bazel:
1. determine commit SHA for the grpc release you want to use
2. Use the [http_archive](https://docs.bazel.build/versions/master/be/workspace.html#http_archive) bazel rule to include gRPC source
  ```
  http_archive(
      name = "com_github_grpc_grpc",
      urls = [
          "https://github.com/grpc/grpc/archive/YOUR_GRPC_COMMIT_SHA.tar.gz",
      ],
      strip_prefix = "grpc-YOUR_GRPC_COMMIT_SHA",
  )

  load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")

  grpc_deps()
  ```

NOTE: currently bazel is only supported for building gRPC on Linux.

## make

Currently the default choice for building on UNIX based systems is `make`.

To install gRPC for C++ on your system using `make`, follow the [Building gRPC C++](../../BUILDING.md)
instructions to build from source and then install locally using `make install`.
This also installs the protocol buffer compiler `protoc` (if you don't have it already),
and the C++ gRPC plugin for `protoc`.

WARNING: After installing with `make install` there is no easy way to uninstall, which can cause issues
if you later want to remove the grpc and/or protobuf installation or upgrade to a newer version.

## cmake

`cmake` is the default build option on Windows, but also works on Linux, MacOS. `cmake` has good
support for crosscompiling and can be used for targeting Android platform.

If your project is using cmake, there are several ways to add gRPC dependency.
- install gRPC via cmake first and then locate it with `find_package(gRPC CONFIG)`. [Example](../../examples/cpp/helloworld/CMakeLists.txt)
- via cmake's `ExternalProject_Add` using a technique called "superbuild". [Example](../../examples/cpp/helloworld/cmake_externalproject/CMakeLists.txt)
- add gRPC source tree to your project (preferrably as a git submodule) and add it to your cmake project with `add_subdirectory`. [Example](../../examples/cpp/helloworld/CMakeLists.txt)

## Packaging systems

There's no standard packaging system for C++. We've looked into supporting some (e.g. Conan and vcpkg) but we are not there yet.
Contributions and community-maintained packages for popular packaging systems are welcome!


## Examples & Additional Documentation

You can find out how to build and run our simplest gRPC C++ example in our
[C++ quick start](../../examples/cpp).

For more detailed documentation on using gRPC in C++ , see our main
documentation site at [grpc.io](https://grpc.io), specifically:

* [Overview](https://grpc.io/docs/): An introduction to gRPC with a simple
  Hello World example in all our supported languages, including C++.
* [gRPC Basics - C++](https://grpc.io/docs/tutorials/basic/c.html):
  A tutorial that steps you through creating a simple gRPC C++ example
  application.
* [Asynchronous Basics - C++](https://grpc.io/docs/tutorials/async/helloasync-cpp.html):
  A tutorial that shows you how to use gRPC C++'s asynchronous/non-blocking
  APIs.


# To start developing gRPC C++

For instructions on how to build gRPC C++ from source, follow the [Building gRPC C++](../../BUILDING.md) instructions.
