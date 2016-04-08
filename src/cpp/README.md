
#Overview

This directory contains source code for C++ implementation of gRPC.

#Status

Beta

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

If you compile gRPC from source, as described below, this also installs the
`protoc` compiler.

If it hasn't been installed, you can run the following commands to install it.

```sh
$ cd grpc/third_party/protobuf
$ sudo make install   # 'make' should have been run by core grpc
```

Alternatively, you can download `protoc` binaries from
[the protocol buffers Github repository](https://github.com/google/protobuf/releases).

#Installation

Currently to install gRPC for C++, you need to build from source as described
below.

#Build from Source

```sh
 $ git clone https://github.com/grpc/grpc.git
 $ cd grpc
 $ git submodule update --init
 $ make
 $ [sudo] make install
```

#Documentation

You can find out how to build and run our simplest gRPC C++ example in our
[C++ quick start](../../examples/cpp).

For more detailed documentation on using gRPC in C++ , see our main
documentation site at [grpc.io](http://grpc.io), specifically:

* [Overview](http://www.grpc.io/docs/): An introduction to gRPC with a simple
  Hello World example in all our supported languages, including C++.
* [gRPC Basics - C++](http://www.grpc.io/docs/tutorials/basic/c.html):
  A tutorial that steps you through creating a simple gRPC C++ example
  application.
* [Asynchronous Basics - C++](http://www.grpc.io/docs/tutorials/async/helloasync-cpp.html):
  A tutorial that shows you how to use gRPC C++'s asynchronous/non-blocking
  APIs.


# Examples

Code examples for gRPC C++ live in this repository's
[examples/cpp](../../examples/cpp) directory.
