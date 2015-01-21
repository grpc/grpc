gRPC - An RPC library and framework
===================================

Copyright 2015 Google Inc.

#Installation

See grpc/INSTALL for installation instructions for various platforms.

#Overview


Remote Procedure Calls (RPCs) provide a useful abstraction for building 
distributed applications and services. The libraries in this repository
provide a concrete implementation of the gRPC protocol, layered over HTTP/2.
These libraries enable communication between clients and servers using any
combination of the supported languages. 


##Interface


Developers using gRPC typically start with the description of an RPC service
(a collection of methods), and generate client and server side interfaces
which they use on the client-side and implement on the server side.

By default, gRPC uses [Protocol Buffers](github.com/google/protobuf) as the
Interface Definition Language (IDL) for describing both the service interface
and the structure of the payload messages. It is possible to use other 
alternatives if desired.

###Surface API
Starting from an interface definition in a .proto file, gRPC provides
Protocol Compiler plugins that generate Client- and Server-side APIs. 
gRPC users typically call into these APIs on the Client side and implement
the corresponding API on the server side.

#### Synchronous vs. Async
Synchronous RPC calls, that block till a response arrives from the server, are
the closest approximation to the abstraction of a procedure call that RPC
aspires to.

On the other hand, networks are inherently asynchronous and in many scenarios,  
it is desirable to have the ability to start RPCs without blocking the current
thread. 

The gRPC programming surface in most languages comes in both Synchronous and
async flavors.


## Streaming

gRPC supports streaming semantics, where either the client or the server (or both)
send a stream of messages on a single RPC call. The most general case is 
Bidirectional Streaming where a single gRPC call establishes a stream where both 
the client and the server can send a stream of messages to each other. The streamed
messages are delivered in the order they were sent.





#Protocol

The gRPC protocol specifies the abstract requirements for communication between
clients and servers. A concrete embedding over HTTP/2 completes the picture by
fleshing out the details of each of the required operations.

## Abstract gRPC protocol
A gRPC RPC comprises of a bidirectional stream of messages, initiated by the client. In the client-to-server direction, this stream begins with a mandatory 'Call Header', followed by optional `Initial-Metadata`, followed by zero or more `Payload Messages`. The server-to-client direction contains an optional `Initial-Metadata`, followed by zero or more `Payload Messages` terminated with a mandatory `Status` and optional `Status-Metadata` (a.k.a.,`Trailing-Metadata').


