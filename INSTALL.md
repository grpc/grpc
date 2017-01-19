#If you are in a hurry

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


#Pre-requisites

##Linux

```sh
 $ [sudo] apt-get install build-essential autoconf libtool
```

##Mac OSX

For a Mac system, git is not available by default. You will first need to
install Xcode from the Mac AppStore and then run the following command from a
terminal:

```sh
 $ [sudo] xcode-select --install
```

##Protoc

By default gRPC uses [protocol buffers](https://github.com/google/protobuf),
you will need the `protoc` compiler to generate stub server and client code.

If you compile gRPC from source, as described below, the Makefile will
automatically try and compile the `protoc` in third_party if you cloned the
repository recursively and it detects that you don't already have it
installed.


#Build from Source

For developers who are interested to contribute, here is how to compile the
gRPC C Core library.

```sh
 $ git clone -b $(curl -L http://grpc.io/release) https://github.com/grpc/grpc
 $ cd grpc
 $ git submodule update --init
 $ make
 $ [sudo] make install
```

Note that if you are interested in contributing, you must install some extra
dependencies to run the test suites. All of these can be found in the Requirements 
for Running Tests section below.

##Windows

There are several ways to build under Windows, of varying complexity depending
on experience with the tools involved.

<!--
###Visual Studio

Versions 2013 and 2015 are both supported. You can use [their respective
community
editions](https://www.visualstudio.com/en-us/downloads/download-visual-studio-vs.aspx).

Building the C Core:
- Open [grpc.sln](https://github.com/grpc/grpc/blob/master/vsprojects/grpc.sln).
- Select your build target.
- Build the `grpc` project.

Building the C++ runtime:
- You need [CMake](https://cmake.org/) on your path to build protobuf (see below
  for building using solely CMake).
- Run `vsprojects/build_protos.bat` (needs `cmake.exe` in your path).
- Open [buildtests_cxx.sln]()
- Select your build target.
- build the `grpc++` project.
-->

###msys2

This approach requires having [msys2](https://msys2.github.io/) installed.

- The Makefile (and source code) should support msys2's mingw32 and mingw64
  compilers. Building with msys2's native compiler is also possible, but
  difficult.
- The Makefile is expecting the Windows versions of OpenSSL (see
  https://slproweb.com/products/Win32OpenSSL.html). It's also possible to build
  the Windows version of OpenSSL from scratch. The output should be `libeay32`
  and `ssleay32`.
- If you are not installing the above files under msys2's path, you may specify
  it, for instance, in the following way:
  ```CPPFLAGS=”-I/c/OpenSSL-Win32/include” LDFLAGS=”-L/c/OpenSSL-Win32/lib” make static_c```
- [protobuf3](https://github.com/google/protobuf/blob/master/src/README.md#c-installation---windows)
  must be installed on the msys2 path.

###Cmake (experimental)

- Install [CMake](https://cmake.org/download/).
- Run it over [grpc's
  CMakeLists.txt](https://github.com/grpc/grpc/blob/master/CMakeLists.txt) to
  generate "projects" for your compiler.
- Build with your compiler of choice. The generated build files should have the
  protobuf3 dependency baked in.
  
#Requirements for Running Tests

Running all of the gRPC tests requires installing a few more dependencies. You will
need python, and the mako template renderer. To install the latter, using pip, one
should simply be able to do `pip install mako`.

In order to run all of the tests we provide, you will need valgrind and clang.
More specifically, under debian, you will need the package libc++-dev to
properly run all the tests.

Compiling and running grpc C++ tests depend on protobuf 3.0.0, gtest and gflags.
Although gflags is provided in third_party, you will need to manually install
that dependency on your system to run these tests. Under a Debian or Ubuntu
system, you can install the gtests and gflags packages using apt-get:

```sh
 $ [sudo] apt-get install libgflags-dev libgtest-dev
```

If you are planning to work on any of the languages other than C and C++, you
will also need their appropriate development environments.

If you want to work under Windows, we recommend the use of Visual Studio 2013.
The [Community or Express editions](http://www.visualstudio.com/en-us/downloads/download-visual-studio-vs.aspx)
are free and suitable for developing with grpc. Note however that our test
environment and tools are available for Unix environments only at the moment.
