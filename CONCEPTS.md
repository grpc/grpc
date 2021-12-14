# gRPC Concepts Overview

Remote Procedure Calls (RPCs) provide a useful abstraction for building
distributed applications and services. The libraries in this repository
provide a concrete implementation of the gRPC protocol, layered over HTTP/2.
These libraries enable communication between clients and servers using any
combination of the supported languages.


## Interface

Developers using gRPC start with a language agnostic description of an RPC service (a collection
of methods). From this description, gRPC will generate client and server side interfaces
in any of the supported languages. The server implements
the service interface, which can be remotely invoked by the client interface.

By default, gRPC uses [Protocol Buffers](https://github.com/protocolbuffers/protobuf) as the
Interface Definition Language (IDL) for describing both the service interface
and the structure of the payload messages. It is possible to use other
alternatives if desired.

### Invoking & handling remote calls
Starting from an interface definition in a .proto file, gRPC provides
Protocol Compiler plugins that generate Client- and Server-side APIs.
gRPC users call into these APIs on the Client side and implement
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
Bidirectional Streaming where a single gRPC call establishes a stream in which both
the client and the server can send a stream of messages to each other. The streamed
messages are delivered in the order they were sent.


# Protocol

The [gRPC protocol](doc/PROTOCOL-HTTP2.md) specifies the abstract requirements for communication between
clients and servers. A concrete embedding over HTTP/2 completes the picture by
fleshing out the details of each of the required operations.

## Abstract gRPC protocol
A gRPC call comprises of a bidirectional stream of messages, initiated by the client. In the client-to-server direction, this stream begins with a mandatory `Call Header`, followed by optional `Initial-Metadata`, followed by zero or more `Payload Messages`. A client signals end of its message stream by means of an underlying lower level protocol. The server-to-client direction contains an optional `Initial-Metadata`, followed by zero or more `Payload Messages` terminated with a mandatory `Status` and optional `Status-Metadata` (a.k.a.,`Trailing-Metadata`).

## Implementation over HTTP/2
The abstract protocol defined above is implemented over [HTTP/2](https://http2.github.io/). gRPC bidirectional streams are mapped to HTTP/2 streams. The contents of `Call Header` and `Initial Metadata` are sent as HTTP/2 headers and subject to HPACK compression. `Payload Messages` are serialized into a byte stream of length prefixed gRPC frames which are then fragmented into HTTP/2 frames at the sender and reassembled at the receiver. `Status` and `Trailing-Metadata` are sent as HTTP/2 trailing headers (a.k.a., trailers). A client signals end of its message stream by setting `END_STREAM` flag on the last DATA frame.
For a detailed description see [doc/PROTOCOL-HTTP2.md](doc/PROTOCOL-HTTP2.md).

## Flow Control
gRPC uses the flow control mechanism in HTTP/2. This enables fine-grained control of memory used for buffering in-flight messages.
