# If you are in a hurry

For language-specific installation instructions for gRPC runtime, please
refer to these documents

 * [C++](examples/cpp): Currently to install gRPC for C++, you need to build from source as described below.
 * [C#](src/csharp): NuGet package `Grpc`
 * [Go](https://github.com/grpc/grpc-go): `go get google.golang.org/grpc`
 * [Java](https://github.com/grpc/grpc-java)
 * [Node](src/node): `npm install grpc`
 * [Objective-C](src/objective-c)
 * [PHP](src/php): `pecl install grpc`
 * [Python](src/python/grpcio): `pip install grpcio`
 * [Ruby](src/ruby): `gem install grpc`


# Pre-requisites

## Linux

```sh
 $ [sudo] apt-get install build-essential autoconf libtool pkg-config
```

If you plan to build from source and run tests, install the following as well:
```sh
 $ [sudo] apt-get install libgflags-dev libgtest-dev
 $ [sudo] apt-get install clang libc++-dev
```

## macOS 

On a Mac, you will first need to
install Xcode or
[Command Line Tools for Xcode](https://developer.apple.com/download/more/)
and then run the following command from a terminal:

```sh
 $ [sudo] xcode-select --install
```

To build gRPC from source, you may also need to install the following
packages, which you can get from [Homebrew](https://brew.sh):

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

## Protoc

By default gRPC uses [protocol buffers](https://github.com/google/protobuf),
you will need the `protoc` compiler to generate stub server and client code.

If you compile gRPC from source, as described below, the Makefile will
automatically try and compile the `protoc` in third_party if you cloned the
repository recursively and it detects that you don't already have it
installed.

If it hasn't been installed, you can run the following commands to install it.

```sh
$ cd grpc/third_party/protobuf
$ sudo make install   # 'make' should have been run by core grpc
```

# Build from Source

For developers who are interested to contribute, here is how to compile the
gRPC C Core library.

```sh
 $ git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc
 $ cd grpc
 $ git submodule update --init
 $ make
 $ [sudo] make install
```

## Windows

There are several ways to build under Windows, of varying complexity depending
on experience with the tools involved.



### Building using CMake (RECOMMENDED)

Builds gRPC C and C++ with boringssl.
- Install Visual Studio 2015 or 2017 (Visual C++ compiler will be used).
- Install [Git](https://git-scm.com/).
- Install [CMake](https://cmake.org/download/).
- Install [Active State Perl](https://www.activestate.com/activeperl/) (`choco install activeperl`) - *required by boringssl*
- Install [Go](https://golang.org/dl/) (`choco install golang`) - *required by boringssl*
- Install [yasm](http://yasm.tortall.net/) and add it to `PATH` (`choco install yasm`) - *required by boringssl*
- (Optional) Install [Ninja](https://ninja-build.org/) (`choco install ninja`)

#### Clone grpc sources including submodules
Before building, you need to clone the gRPC github repository and download submodules containing source code 
for gRPC's dependencies (that's done by the `submodule` command).
```
> @rem You can also do just "git clone --recursive -b THE_BRANCH_YOU_WANT https://github.com/grpc/grpc"
> powershell git clone --recursive -b ((New-Object System.Net.WebClient).DownloadString(\"https://grpc.io/release\").Trim()) https://github.com/grpc/grpc
> cd grpc
> @rem To update submodules at later time, run "git submodule update --init"
```

#### cmake: Using Visual Studio 2015 or 2017 (can only build with OPENSSL_NO_ASM).
When using the "Visual Studio" generator,
cmake will generate a solution (`grpc.sln`) that contains a VS project for 
every target defined in `CMakeLists.txt` (+ few extra convenience projects
added automatically by cmake). After opening the solution with Visual Studio 
you will be able to browse and build the code as usual.
```
> @rem Run from grpc directory after cloning the repo with --recursive or updating submodules.
> md .build
> cd .build
> cmake .. -G "Visual Studio 14 2015" -DCMAKE_BUILD_TYPE=Release
> cmake --build .
```

#### cmake: Using Ninja (faster build, supports boringssl's assembly optimizations).
Please note that when using Ninja, you'll still need Visual C++ (part of Visual Studio)
installed to be able to compile the C/C++ sources.
```
> @rem Run from grpc directory after cloning the repo with --recursive or updating submodules.
> md .build
> cd .build
> call "%VS140COMNTOOLS%..\..\VC\vcvarsall.bat" x64
> cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release
> cmake --build .
> ninja install
```

### msys2 (with mingw)

The Makefile (and source code) should support msys2's mingw32 and mingw64
compilers. Building with msys2's native compiler is also possible, but
difficult.

This approach requires having [msys2](https://msys2.github.io/) installed.

```
# Install prerequisites
MSYS2$ pacman -S autoconf automake gcc libtool mingw-w64-x86_64-toolchain perl pkg-config zlib
MSYS2$ pacman -S mingw-w64-x86_64-gflags
```

```
# From mingw shell
MINGW64$ export CPPFLAGS="-D_WIN32_WINNT=0x0600"
MINGW64$ make
```

NOTE: While most of the make targets are buildable under Mingw, some haven't been ported to Windows yet
and may fail to build (mostly trying to include POSIX headers not available on Mingw).

### Pre-generated Visual Studio solution (DELETED)

*WARNING: This used to be the recommended way to build on Windows, but because of significant limitations (hard to build dependencies including boringssl, .proto codegen is hard to support, ..) we are no longer providing them. Use cmake to build on Windows instead.*
