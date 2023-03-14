# gRPC C++

This directory contains the C++ implementation of gRPC.

# To start using gRPC C++

This section describes how to add gRPC as a dependency to your C++ project.

In the C++ world, there's no universally accepted standard for managing project dependencies.
Therefore, gRPC supports several major build systems, which should satisfy most users.

## Supported Platforms

* Officially Supported: These platforms are officially supported. We follow
  [the OSS Foundational C++ Support Policy](https://opensource.google/documentation/policies/cplusplus-support)
  to choose platforms to support.
  We test our code on these platform and have automated continuous integration tests for
  them.
  .

* Best Effort: We do not have continous integration tests for these, but we are
  fairly confident that gRPC C++ would work on them. We will make our best
  effort to support them, and we welcome patches for such platforms, but we
  might need to declare bankruptcy on some issues.

* Community Supported: These platforms are supported by contributions from the
  open source community, there is no official support for them. Breakages on
  these platforms may go unnoticed, and the community is responsible for all
  maintenance. Unmaintained code for these platforms may be deleted.

| Operating System | Architectures | Versions | Support Level |
|------------------|---------------|----------|---------------|
| Linux - Debian, Ubuntu, CentOS | x86, x64      | clang 6+, GCC 7.3+     | Officially Supported |
| Windows 10+                    | x86, x64      | Visual Studio 2019+    | Officially Supported |
| MacOS                          | x86, x64      | XCode 12+              | Officially Supported |
| Linux - Others                 | x86, x64      | clang 6+, GCC 7.3+     | Best Effort          |
| Linux                          | ARM           |                        | Best Effort          |
| iOS                            |               |                        | Best Effort          |
| Android                        |               |                        | Best Effort          |
| Asylo                          |               |                        | Best Effort          |
| FreeBSD                        |               |                        | Community Supported  |
| NetBSD                         |               |                        | Community Supported  |
| OpenBSD                        |               |                        | Community Supported  |
| AIX                            |               |                        | Community Supported  |
| Solaris                        |               |                        | Community Supported  |
| NaCL                           |               |                        | Community Supported  |
| Fuchsia                        |               |                        | Community Supported  |

## Bazel

Bazel is the primary build system used by the core gRPC development team. Bazel
provides fast builds and it easily handles dependencies that support bazel.

To add gRPC as a dependency in bazel:
1. determine commit SHA for the grpc release you want to use
2. Use the [http_archive](https://docs.bazel.build/versions/master/repo/http.html#http_archive) bazel rule to include gRPC source
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
  load("@com_github_grpc_grpc//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")
  grpc_extra_deps()
  ```

## CMake

`cmake` is your best option if you cannot use bazel. It supports building on Linux,
MacOS and Windows (official support) but also has a good chance of working on
other platforms (no promises!). `cmake` has good support for crosscompiling and
can be used for targeting the Android platform.

To build gRPC C++ from source, follow the [BUILDING guide](../../BUILDING.md).

### find_package

The canonical way to discover dependencies in CMake is the
[`find_package` command](https://cmake.org/cmake/help/latest/command/find_package.html).

```cmake
find_package(gRPC CONFIG REQUIRED)
add_executable(my_exe my_exe.cc)
target_link_libraries(my_exe gRPC::grpc++)
```
[Full example](../../examples/cpp/helloworld/CMakeLists.txt)

`find_package` can only find software that has already been installed on your
system. In practice that means you'll need to install gRPC using cmake first.
gRPC's cmake support provides the option to install gRPC either system-wide
(not recommended) or under a directory prefix in a way that you can later
easily use it with the `find_package(gRPC CONFIG REQUIRED)` command.

The following sections describe strategies to automatically build gRPC
as part of your project.

### FetchContent
If you are using CMake v3.11 or newer you should use CMake's
[FetchContent module](https://cmake.org/cmake/help/latest/module/FetchContent.html).
The first time you run CMake in a given build directory, FetchContent will
clone the gRPC repository and its submodules. `FetchContent_MakeAvailable()`
also sets up an `add_subdirectory()` rule for you. This causes gRPC to be
built as part of your project.

```cmake
cmake_minimum_required(VERSION 3.15)
project(my_project)

include(FetchContent)
FetchContent_Declare(
  gRPC
  GIT_REPOSITORY https://github.com/grpc/grpc
  GIT_TAG        RELEASE_TAG_HERE  # e.g v1.28.0
)
set(FETCHCONTENT_QUIET OFF)
FetchContent_MakeAvailable(gRPC)

add_executable(my_exe my_exe.cc)
target_link_libraries(my_exe grpc++)
```

Note that you need to
[install the prerequisites](../../BUILDING.md#pre-requisites)
before building gRPC.

### git submodule
If you cannot use FetchContent, another approach is to add the gRPC source tree
to your project as a
[git submodule](https://git-scm.com/book/en/v2/Git-Tools-Submodules).
You can then add it to your CMake project with `add_subdirectory()`.
[Example](../../examples/cpp/helloworld/CMakeLists.txt)

### Support system-installed gRPC

If your project builds gRPC you should still consider the case where a user
wants to build your software using a previously installed gRPC. Here's a
code snippet showing how this is typically done.

```cmake
option(USE_SYSTEM_GRPC "Use system installed gRPC" OFF)
if(USE_SYSTEM_GRPC)
  # Find system-installed gRPC
  find_package(gRPC CONFIG REQUIRED)
else()
  # Build gRPC using FetchContent or add_subdirectory
endif()
```

[Full example](../../examples/cpp/helloworld/CMakeLists.txt)

## pkg-config

If your project does not use CMake (e.g. you're using `make` directly), you can
first install gRPC C++ using CMake, and have your non-CMake project rely on the
`pkgconfig` files which are provided by gRPC installation.
[Example](../../test/distrib/cpp/run_distrib_test_cmake_pkgconfig.sh)

**Note for CentOS 7 users**

CentOS-7 ships with `pkg-config` 0.27.1, which has a
[bug](https://bugs.freedesktop.org/show_bug.cgi?id=54716) that can make
invocations take extremely long to complete. If you plan to use `pkg-config`,
you'll want to upgrade it to something newer.

## make (deprecated)

The default choice for building on UNIX based systems used to be `make`, but we are no longer recommending it.
You should use `bazel` or `cmake` instead.

To install gRPC for C++ on your system using `make`, follow the [Building gRPC C++](../../BUILDING.md)
instructions to build from source and then install locally using `make install`.
This also installs the protocol buffer compiler `protoc` (if you don't have it already),
and the C++ gRPC plugin for `protoc`.

WARNING: After installing with `make install` there is no easy way to uninstall, which can cause issues
if you later want to remove the grpc and/or protobuf installation or upgrade to a newer version.

## Packaging systems

We do not officially support any packaging system for C++, but there are some community-maintained packages that are kept up-to-date
and are known to work well. More contributions and support for popular packaging systems are welcome!

### Install using vcpkg package
gRPC is available using the [vcpkg](https://github.com/Microsoft/vcpkg) dependency manager:

```
# install vcpkg package manager on your system using the official instructions
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg

# Bootstrap on Linux:
./bootstrap-vcpkg.sh
# Bootstrap on Windows instead:
# ./bootstrap-vcpkg.bat

./vcpkg integrate install

# install gRPC using vcpkg package manager
./vcpkg install grpc
```

The gRPC port in vcpkg is kept up to date by Microsoft team members and community contributors. If the version is out of date, please [create an issue or pull request](https://github.com/Microsoft/vcpkg) on the vcpkg repository.


## Examples & Additional Documentation

You can find out how to build and run our simplest gRPC C++ example in our
[C++ quick start](../../examples/cpp).

For more detailed documentation on using gRPC in C++ , see our main
documentation site at [grpc.io](https://grpc.io), specifically:

* [Overview](https://grpc.io/docs): An introduction to gRPC with a simple
  Hello World example in all our supported languages, including C++.
* [gRPC Basics - C++](https://grpc.io/docs/languages/cpp/basics):
  A tutorial that steps you through creating a simple gRPC C++ example
  application.
* [Asynchronous Basics - C++](https://grpc.io/docs/languages/cpp/async):
  A tutorial that shows you how to use gRPC C++'s asynchronous/non-blocking
  APIs.


# To start developing gRPC C++

For instructions on how to build gRPC C++ from source, follow the [Building gRPC C++](../../BUILDING.md) instructions.
