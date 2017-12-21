[![Build Status](https://grpc-testing.appspot.com/job/gRPC_master/badge/icon)](https://grpc-testing.appspot.com/job/gRPC_master)

[gRPC - An RPC library and framework](http://github.com/grpc/grpc)
===================================

[![Join the chat at https://gitter.im/grpc/grpc](https://badges.gitter.im/grpc/grpc.svg)](https://gitter.im/grpc/grpc?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

Copyright 2015
[The gRPC Authors](https://github.com/grpc/grpc/blob/master/AUTHORS)

# Documentation

You can find more detailed documentation and examples in the [doc](doc) and [examples](examples) directories respectively.

# Installation & Testing

See [INSTALL](INSTALL.md) for installation instructions for various platforms.

See [tools/run_tests](tools/run_tests) for more guidance on how to run various test suites (e.g. unit tests, interop tests, benchmarks)

See [Performance dashboard](http://performance-dot-grpc-testing.appspot.com/explore?dashboard=5636470266134528) for the performance numbers for the latest released version.

# Repository Structure & Status

This repository contains source code for gRPC libraries for multiple languages written on top of shared C core library [src/core](src/core).

Libraries in different languages may be in different states of development. We are seeking contributions for all of these libraries.

| Language                | Source                              |
|-------------------------|-------------------------------------|
| Shared C [core library] | [src/core](src/core)                |
| C++                     | [src/cpp](src/cpp)                  |
| Ruby                    | [src/ruby](src/ruby)                |
| Python                  | [src/python](src/python)            |
| PHP                     | [src/php](src/php)                  |
| C#                      | [src/csharp](src/csharp)            |
| Objective-C             | [src/objective-c](src/objective-c)  |

Java source code is in the [grpc-java](http://github.com/grpc/grpc-java)
repository. Go source code is in the
[grpc-go](http://github.com/grpc/grpc-go) repository. NodeJS source code is in the
[grpc-node](https://github.com/grpc/grpc-node) repository.

See [MANIFEST.md](MANIFEST.md) for a listing of top-level items in the
repository.

# Overview


Remote Procedure Calls (RPCs) provide a useful abstraction for building
distributed applications and services. The libraries in this repository
provide a concrete implementation of the gRPC protocol, layered over HTTP/2.
These libraries enable communication between clients and servers using any
combination of the supported languages.


## Interface


Developers using gRPC typically start with the description of an RPC service
(a collection of methods), and generate client and server side interfaces
which they use on the client-side and implement on the server side.

By default, gRPC uses [Protocol Buffers](https://github.com/google/protobuf) as the
Interface Definition Language (IDL) for describing both the service interface
and the structure of the payload messages. It is possible to use other
alternatives if desired.

### Surface API
Starting from an interface definition in a .proto file, gRPC provides
Protocol Compiler plugins that generate Client- and Server-side APIs.
gRPC users typically call into these APIs on the Client side and implement
the corresponding API on the server side.

#### Synchronous vs. asynchronous
Synchronous RPC calls, that block until a response arrives from the server, are
the closest approximation to the abstraction of a procedure call that RPC
aspires to.

On the other hand, networks are inherently asynchronous and in many scenarios,
it is desirable to have the ability to start RPCs without blocking the current
thread.

The gRPC programming surface in most languages comes in both synchronous and
asynchronous flavors.


## Streaming

gRPC supports streaming semantics, where either the client or the server (or both)
send a stream of messages on a single RPC call. The most general case is
Bidirectional Streaming where a single gRPC call establishes a stream where both
the client and the server can send a stream of messages to each other. The streamed
messages are delivered in the order they were sent.


# Protocol

The [gRPC protocol](doc/PROTOCOL-HTTP2.md) specifies the abstract requirements for communication between
clients and servers. A concrete embedding over HTTP/2 completes the picture by
fleshing out the details of each of the required operations.

## Abstract gRPC protocol
A gRPC RPC comprises of a bidirectional stream of messages, initiated by the client. In the client-to-server direction, this stream begins with a mandatory `Call Header`, followed by optional `Initial-Metadata`, followed by zero or more `Payload Messages`. The server-to-client direction contains an optional `Initial-Metadata`, followed by zero or more `Payload Messages` terminated with a mandatory `Status` and optional `Status-Metadata` (a.k.a.,`Trailing-Metadata`).

## Implementation over HTTP/2
The abstract protocol defined above is implemented over [HTTP/2](https://http2.github.io/). gRPC bidirectional streams are mapped to HTTP/2 streams. The contents of `Call Header` and `Initial Metadata` are sent as HTTP/2 headers and subject to HPACK compression. `Payload Messages` are serialized into a byte stream of length prefixed gRPC frames which are then fragmented into HTTP/2 frames at the sender and reassembled at the receiver. `Status` and `Trailing-Metadata` are sent as HTTP/2 trailing headers (a.k.a., trailers).

## Flow Control
gRPC inherits the flow control mechanisms in HTTP/2 and uses them to enable fine-grained control of the amount of memory used for buffering in-flight messages.
