gRPC C++ - Building from source
===========================

This document has detailed instructions on how to build gRPC C++ from source. Note that it only covers the build of gRPC itself and is meant for gRPC C++ contributors and/or power users. 
Other should follow the user instructions. See the [How to use](https://github.com/grpc/grpc/tree/master/src/cpp#to-start-using-grpc-c) instructions for guidance on how to add gRPC as a dependency to a C++ application (there are several ways and system-wide installation is often not the best choice).

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
 $ # clang and LLVM C++ lib is only required for sanitizer builds
 $ [sudo] apt-get install clang libc++-dev
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

*Tip*: when building,
you *may* want to explicitly set the `LIBTOOL` and `LIBTOOLIZE`
environment variables when running `make` to ensure the version
installed by `brew` is being used:

```sh
 $ LIBTOOL=glibtool LIBTOOLIZE=glibtoolize make
```

## Windows

To prepare for cmake + Microsoft Visual C++ compiler build
- Install Visual Studio 2019 or later (Visual C++ compiler will be used).
- Install [Git](https://git-scm.com/).
- Install [CMake](https://cmake.org/download/).
- Install [nasm](https://www.nasm.us/) and add it to `PATH` (`choco install nasm`) - *required by boringssl*
- (Optional) Install [Ninja](https://ninja-build.org/) (`choco install ninja`)

# Clone the repository (including submodules)

Before building, you need to clone the gRPC github repository and download submodules containing source code
for gRPC's dependencies (that's done by the `submodule` command or `--recursive` flag). Use following commands
to clone the gRPC repository at the [latest stable release tag](https://github.com/grpc/grpc/releases)

## Unix

```sh
 $ git clone -b RELEASE_TAG_HERE https://github.com/grpc/grpc
 $ cd grpc
 $ git submodule update --init
 ```

## Windows

```
> git clone -b RELEASE_TAG_HERE https://github.com/grpc/grpc
> cd grpc
> git submodule update --init
```

NOTE: The `bazel` build tool uses a different model for dependencies. You only need to worry about downloading submodules if you're building
with something else than `bazel` (e.g. `cmake`).

# Build from source

In the C++ world, there's no "standard" build system that would work for all supported use cases and on all supported platforms.
Therefore, gRPC supports several major build systems, which should satisfy most users. Depending on your needs
we recommend building using `bazel` or `cmake`.

## Building with bazel (recommended)

Bazel is the primary build system for gRPC C++. If you're comfortable using bazel, we can certainly recommend it.
Using bazel will give you the best developer experience in addition to faster and cleaner builds.

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

NOTE: If you are a gRPC maintainer and you have access to our test cluster, you should use our [gRPC's Remote Execution environment](tools/remote_build/README.md)
to get significant improvement to the build and test speed (and a bunch of other very useful features).

## Building with CMake

### Linux/Unix, Using Make

Run from the grpc directory after cloning the repo with --recursive or updating submodules.
```
$ mkdir -p cmake/build
$ cd cmake/build
$ cmake ../..
$ make
```

If you want to build shared libraries (`.so` files), run `cmake` with `-DBUILD_SHARED_LIBS=ON`.

### Windows, Using Visual Studio 2019 or later

When using the "Visual Studio" generator,
cmake will generate a solution (`grpc.sln`) that contains a VS project for
every target defined in `CMakeLists.txt` (+ a few extra convenience projects
added automatically by cmake). After opening the solution with Visual Studio
you will be able to browse and build the code.
```
> @rem Run from grpc directory after cloning the repo with --recursive or updating submodules.
> md .build
> cd .build
> cmake .. -G "Visual Studio 16 2019"
> cmake --build . --config Release
```

Using gRPC C++ as a DLL is not recommended, but you can still enable it by running `cmake` with `-DBUILD_SHARED_LIBS=ON`. 

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

Using gRPC C++ as a DLL is not recommended, but you can still enable it by running `cmake` with `-DBUILD_SHARED_LIBS=ON`.

### Windows: A note on building shared libs (DLLs)

Windows DLL build is supported at a "best effort" basis and we don't recommend using gRPC C++ as a DLL as there are some known drawbacks around how C++ DLLs work on Windows. For example, there is no stable C++ ABI and you can't safely allocate memory in one DLL, and free it in another etc.

That said, we don't actively prohibit building DLLs on windows (it can be enabled in cmake with `-DBUILD_SHARED_LIBS=ON`), and are free to use the DLL builds
at your own risk.
- you've been warned that there are some important drawbacks and some things might not work at all or will be broken in interesting ways.
- we don't have extensive testing for DLL builds in place (to avoid maintenance costs, increased test duration etc.) so regressions / build breakages might occur

### Dependency management

gRPC's CMake build system has two options for handling dependencies.
CMake can build the dependencies for you, or it can search for libraries
that are already installed on your system and use them to build gRPC.

This behavior is controlled by the `gRPC_<depname>_PROVIDER` CMake variables,
e.g. `gRPC_CARES_PROVIDER`. The options that these variables take are as follows:

* module - build dependencies alongside gRPC. The source code is obtained from
gRPC's git submodules.
* package - use external copies of dependencies that are already available
on your system. These could come from your system package manager, or perhaps
you pre-installed them using CMake with the `CMAKE_INSTALL_PREFIX` option.

For example, if you set `gRPC_CARES_PROVIDER=module`, then CMake will build
c-ares before building gRPC. On the other hand, if you set
`gRPC_CARES_PROVIDER=package`, then CMake will search for a copy of c-ares
that's already installed on your system and use it to build gRPC.

### Install after build

Perform the following steps to install gRPC using CMake.
* Set `-DgRPC_INSTALL=ON`
* Build the `install` target

The install destination is controlled by the
[`CMAKE_INSTALL_PREFIX`](https://cmake.org/cmake/help/latest/variable/CMAKE_INSTALL_PREFIX.html) variable.

If you are running CMake v3.13 or newer you can build gRPC's dependencies
in "module" mode and install them alongside gRPC in a single step.
[Example](test/distrib/cpp/run_distrib_test_cmake_module_install.sh)

If you are building gRPC < 1.27 or if you are using CMake < 3.13 you will need
to select "package" mode (rather than "module" mode) for the dependencies.
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
              -DgRPC_RE2_PROVIDER=package      \
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

[Cross-compile example](test/distrib/cpp/run_distrib_test_cmake_aarch64_cross.sh)

### A note on SONAME and its ABI compatibility implications in the cmake build

Best efforts are made to bump the SONAME revision during ABI breaches. While a
change in the SONAME clearly indicates an ABI incompatibility, no hard guarantees
can be made about any sort of ABI stability across the same SONAME version.

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

By default gRPC uses [protocol buffers](https://github.com/protocolbuffers/protobuf),
you will need the `protoc` compiler to generate stub server and client code.

If you compile gRPC from source, as described above, the Makefile will
automatically try compiling the `protoc` in third_party if you cloned the
repository recursively and it detects that you do not already have 'protoc' compiler
installed.
