gRPC C++ - Building from source
===========================

This document has detailed instructions on how to build gRPC C++ from source. Note that it only covers the build of gRPC itself and is mostly meant for gRPC C++ contributors and/or power users.
Other should follow the user instructions. See the [How to use](https://github.com/grpc/grpc/tree/master/src/cpp#to-start-using-grpc-c) instructions for guidance on how to add gRPC as a dependency to a C++ application (there are several ways and system wide installation is often not the best choice).

# Pre-requisites

## Linux

```sh
 $ [sudo] apt-get install build-essential autoconf libtool pkg-config
```

If you plan to build using CMake
```sh
 $ [sudo] apt-get install cmake
```

If you are a contributor and plan to build and run tests, install the following as well:
```sh
 $ # libgflags-dev is only required if building with make (deprecated)
 $ [sudo] apt-get install libgflags-dev
 $ # clang and LLVM C++ lib is only required for sanitizer builds
 $ [sudo] apt-get install clang-5.0 libc++-dev
```

## MacOS

On a Mac, you will first need to
install Xcode or
[Command Line Tools for Xcode](https://developer.apple.com/download/more/)
and then run the following command from a terminal:

```sh
 $ [sudo] xcode-select --install
```

To build gRPC from source, you may need to install the following
packages from [Homebrew](https://brew.sh):

```sh
 $ brew install autoconf automake libtool shtool
```

If you plan to build using CMake, follow the instructions from https://cmake.org/download/

If you are a contributor and plan to build and run tests, install the following as well:
```sh
 $ # gflags is only required if building with make (deprecated) 
 $ brew install gflags
```

*Tip*: when building,
you *may* want to explicitly set the `LIBTOOL` and `LIBTOOLIZE`
environment variables when running `make` to ensure the version
installed by `brew` is being used:

```sh
 $ LIBTOOL=glibtool LIBTOOLIZE=glibtoolize make
```

## Windows

To prepare for cmake + Microsoft Visual C++ compiler build
- Install Visual Studio 2015 or 2017 (Visual C++ compiler will be used).
- Install [Git](https://git-scm.com/).
- Install [CMake](https://cmake.org/download/).
- Install [nasm](https://www.nasm.us/) and add it to `PATH` (`choco install nasm`) - *required by boringssl*
- (Optional) Install [Ninja](https://ninja-build.org/) (`choco install ninja`)

# Clone the repository (including submodules)

Before building, you need to clone the gRPC github repository and download submodules containing source code
for gRPC's dependencies (that's done by the `submodule` command or `--recursive` flag). The following commands will clone the gRPC
repository at the latest stable version.

## Unix

```sh
 $ git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc
 $ cd grpc
 $ git submodule update --init
 ```

## Windows

```
> @rem You can also do just "git clone --recursive -b THE_BRANCH_YOU_WANT https://github.com/grpc/grpc"
> powershell git clone --recursive -b ((New-Object System.Net.WebClient).DownloadString(\"https://grpc.io/release\").Trim()) https://github.com/grpc/grpc
> cd grpc
> @rem To update submodules at later time, run "git submodule update --init"
```

NOTE: The `bazel` build tool uses a different model for dependencies. You only need to worry about downloading submodules if you're building
with something else than `bazel` (e.g. `cmake`).

# Build from source

In the C++ world, there's no "standard" build system that would work for in all supported use cases and on all supported platforms.
Therefore, gRPC supports several major build systems, which should satisfy most users. Depending on your needs
we recommend building using `bazel` or `cmake`.

## Building with bazel (recommended)

Bazel is the primary build system for gRPC C++ and if you're comfortable with using bazel, we can certainly recommend it.
Using bazel will give you the best developer experience as well as faster and cleaner builds.

You'll need `bazel` version `1.0.0` or higher to build gRPC.
See [Installing Bazel](https://docs.bazel.build/versions/master/install.html) for instructions how to install bazel on your system.
We support building with `bazel` on Linux, MacOS and Windows.

From the grpc repository root
```
# Build gRPC C++
$ bazel build :all
```

```
# Run all the C/C++ tests
$ bazel test --config=dbg //test/...
```

NOTE: If you are gRPC maintainer and you have access to our test cluster, you should use the our [gRPC's Remote Execution environment](tools/remote_build/README.md)
to get significant improvement to the build and test speed (and a bunch of other very useful features).

## Building with CMake

### Linux/Unix, Using Make

Run from grpc directory after cloning the repo with --recursive or updating submodules.
```
$ mkdir -p cmake/build
$ cd cmake/build
$ cmake ../..
$ make
```

If you want to build shared libraries (`.so` files), run `cmake` with `-DBUILD_SHARED_LIBS=ON`.

### Windows, Using Visual Studio 2015 or 2017

When using the "Visual Studio" generator,
cmake will generate a solution (`grpc.sln`) that contains a VS project for
every target defined in `CMakeLists.txt` (+ few extra convenience projects
added automatically by cmake). After opening the solution with Visual Studio
you will be able to browse and build the code.
```
> @rem Run from grpc directory after cloning the repo with --recursive or updating submodules.
> md .build
> cd .build
> cmake .. -G "Visual Studio 14 2015"
> cmake --build . --config Release
```

If you want to build DLLs, run `cmake` with `-DBUILD_SHARED_LIBS=ON`.

### Windows, Using Ninja (faster build).

Please note that when using Ninja, you will still need Visual C++ (part of Visual Studio)
installed to be able to compile the C/C++ sources.
```
> @rem Run from grpc directory after cloning the repo with --recursive or updating submodules.
> cd cmake
> md build
> cd build
> call "%VS140COMNTOOLS%..\..\VC\vcvarsall.bat" x64
> cmake ..\.. -GNinja -DCMAKE_BUILD_TYPE=Release
> cmake --build .
```

If you want to build DLLs, run `cmake` with `-DBUILD_SHARED_LIBS=ON`.

### Dependency management

gRPC's CMake build system provides two modes for handling dependencies.
* module - build dependencies alongside gRPC.
* package - use external copies of dependencies that are already available
on your system.

This behavior is controlled by the `gRPC_<depname>_PROVIDER` CMake variables,
ie `gRPC_CARES_PROVIDER`.

### Install after build

Perform the following steps to install gRPC using CMake.
* Set `-DgRPC_INSTALL=ON`
* Build the `install` target

The install destination is controlled by the
[`CMAKE_INSTALL_PREFIX`](https://cmake.org/cmake/help/latest/variable/CMAKE_INSTALL_PREFIX.html) variable.

If you are running CMake v3.13 or newer you can build gRPC's dependencies
in "module" mode and install them alongside gRPC in a single step.
[Example](test/distrib/cpp/run_distrib_test_cmake_module_install.sh)

If you are using an older version of gRPC, you will need to select "package"
mode (rather than "module" mode) for the dependencies.
This means you will need to have external copies of these libraries available
on your system. This [example](test/distrib/cpp/run_distrib_test_cmake.sh) shows
how to install dependencies with cmake before proceeding to installing gRPC itself. 

```
# NOTE: all of gRPC's dependencies need to be already installed
$ cmake ../.. -DgRPC_INSTALL=ON                \
              -DCMAKE_BUILD_TYPE=Release       \
              -DgRPC_ABSL_PROVIDER=package     \
              -DgRPC_CARES_PROVIDER=package    \
              -DgRPC_PROTOBUF_PROVIDER=package \
              -DgRPC_SSL_PROVIDER=package      \
              -DgRPC_ZLIB_PROVIDER=package
$ make
$ make install
```

### Cross-compiling

You can use CMake to cross-compile gRPC for another architecture. In order to
do so, you will first need to build `protoc` and `grpc_cpp_plugin`
for the host architecture. These tools are used during the build of gRPC, so
we need copies of executables that can be run natively.

You will likely need to install the toolchain for the platform you are
targeting for your cross-compile. Once you have done so, you can write a
toolchain file to tell CMake where to find the compilers and system tools
that will be used for this build.

This toolchain file is specified to CMake by setting the `CMAKE_TOOLCHAIN_FILE`
variable.
```
$ cmake ../.. -DCMAKE_TOOLCHAIN_FILE=path/to/file
$ make
```

[Cross-compile example](test/distrib/cpp/run_distrib_test_raspberry_pi.sh)

## Building with make on UNIX systems (deprecated)

NOTE: `make` used to be gRPC's default build system, but we're no longer recommending it. You should use `bazel` or `cmake` instead. The `Makefile` is only intended for internal usage and is not meant for public consumption.

From the grpc repository root
```sh
 $ make
```

NOTE: if you get an error on linux such as 'aclocal-1.15: command not found', which can happen if you ran 'make' before installing the pre-reqs, try the following:
```sh
$ git clean -f -d -x && git submodule foreach --recursive git clean -f -d -x
$ [sudo] apt-get install build-essential autoconf libtool pkg-config
$ make
```

### A note on `protoc`

By default gRPC uses [protocol buffers](https://github.com/google/protobuf),
you will need the `protoc` compiler to generate stub server and client code.

If you compile gRPC from source, as described below, the Makefile will
automatically try compiling the `protoc` in third_party if you cloned the
repository recursively and it detects that you do not already have 'protoc' compiler
installed.
