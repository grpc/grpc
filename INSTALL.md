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
 $ [sudo] apt-get install build-essential autoconf libtool
```

If you plan to build from source and run tests, install the following as well:
```sh
 $ [sudo] apt-get install libgflags-dev libgtest-dev
 $ [sudo] apt-get install clang libc++-dev
```

## Mac OSX

For a Mac system, git is not available by default. You will first need to
install Xcode from the Mac AppStore and then run the following command from a
terminal:

```sh
 $ [sudo] xcode-select --install
```

## Protoc

By default gRPC uses [protocol buffers](https://github.com/google/protobuf),
you will need the `protoc` compiler to generate stub server and client code.

If you compile gRPC from source, as described below, the Makefile will
automatically try and compile the `protoc` in third_party if you cloned the
repository recursively and it detects that you don't already have it
installed.


# Build from Source

For developers who are interested to contribute, here is how to compile the
gRPC C Core library.

```sh
 $ git clone -b $(curl -L http://grpc.io/release) https://github.com/grpc/grpc
 $ cd grpc
 $ git submodule update --init
 $ make
 $ [sudo] make install
```

## Windows

There are several ways to build under Windows, of varying complexity depending
on experience with the tools involved.

### Pre-generated Visual Studio solution

The pre-generated VS projects & solution are checked into the repository under the [vsprojects](/vsprojects) directory.

### Building using CMake (with BoringSSL)
- Install [CMake](https://cmake.org/download/).
- Install [Active State Perl](http://www.activestate.com/activeperl/) (`choco install activeperl`)
- Install [Ninja](https://ninja-build.org/) (`choco install ninja`)
- Install [Go](https://golang.org/dl/) (`choco install golang`)
- Install [yasm](http://yasm.tortall.net/) and add it to `PATH` (`choco install yasm`)
- Run these commands in the repo root directory
```
> md .build
> cd .build
> call "%VS140COMNTOOLS%..\..\VC\vcvarsall.bat" x64
> cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release
> cmake --build .
```
NOTE: Currently you can only use Ninja to build using cmake on Windows (because of the boringssl dependency).

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
