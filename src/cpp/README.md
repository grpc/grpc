
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

If you compile gRPC from source, as described below, the Makefile will
automatically try and compile the `protoc` in third party if you cloned the
repository recursively and it detects that you don't already have it
installed.

If it hasn't been installed, you can run the following commands to install it.

```sh
$ cd grpc/third_party/protobuf
$ sudo make install   # 'make' should have been run by core grpc
```

Alternatively, you can download `protoc` binaries from
[the protocol buffers Github repository](https://github.com/google/protobuf/releases).

#INSTALLATION

Currently to install gRPC for C++, you need to build from source as described below.

#Build from Source

```sh
 $ git clone https://github.com/grpc/grpc.git
 $ cd grpc
 $ git submodule update --init
 $ make
 $ [sudo] make install
```

#DOCUMENTATION

- The gRPC C++ refenrence documentation is available online at
  [grpc.io](http://www.grpc.io/docs/tutorials/basic/c.html)
- [Helloworld example](../../examples/cpp/helloworld)

