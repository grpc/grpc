gRPC C++ - Building from source
===========================

This document has detailed instructions on how to build gRPC C++ from source. Note that it only covers the build of gRPC itself and is mostly meant for gRPC C++ contributors and/or power users.
Other should follow the user instructions. See the [How to use](https://github.com/grpc/grpc/tree/master/src/cpp#to-start-using-grpc-c) instructions for guidance on how to add gRPC as a dependency to a C++ application (there are several ways and system wide installation is often not the best choice).

# Pre-requisites

## Linux

```sh
 $ [sudo] apt-get install build-essential autoconf libtool pkg-config
```

If you plan to build from source and run tests, install the following as well:
```sh
 $ [sudo] apt-get install libgflags-dev libgtest-dev
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

If you plan to build from source and run tests, install the following as well:
```sh
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
- Install [Active State Perl](https://www.activestate.com/activeperl/) (`choco install activeperl`) - *required by boringssl*
- Install [Go](https://golang.org/dl/) (`choco install golang`) - *required by boringssl*
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

## CMake: Linux/Unix, Using Make

Run from grpc directory after cloning the repo with --recursive or updating submodules.
```
$ mkdir -p cmake/build
$ cd cmake/build
$ cmake ../..
$ make
```

If you want to build shared libraries (`.so` files), run `cmake` with `-DBUILD_SHARED_LIBS=ON`.

## Building with CMake: Windows, Using Visual Studio 2015 or 2017 (can only build with OPENSSL_NO_ASM).

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

## Building with CMake: Windows, Using Ninja (faster build, supports boringssl's assembly optimizations).

Please note that when using Ninja, you will still need Visual C++ (part of Visual Studio)
installed to be able to compile the C/C++ sources.
```
> @rem Run from grpc directory after cloning the repo with --recursive or updating submodules.
> md .build
> cd .build
> call "%VS140COMNTOOLS%..\..\VC\vcvarsall.bat" x64
> cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release
> cmake --build .
```

## Building with make (on UNIX systems)

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
