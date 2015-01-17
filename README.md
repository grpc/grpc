gRPC - An RPC library and framework
===================================

Copyright 2015 Google Inc.

Installation
------------

See grpc/INSTALL for installation instructions for various platforms.

Overview
--------

Remote Procedure Calls (RPCs) provide a useful abstraction for building 
distributed applications and services. The libraries in this repository
provide a concrete implementation of the gRPC protocol, layered over HTTP/2.
These libraries enable communication between clients and servers using any
combination of the supported languages. 

Developers using gRPC typically start with the description of an RPC service
(a collection of methods), and generate client and server side interfaces
which they use on the client-side and implement on the server side.

Protocol
--------

The gRPC protocol specifies the abstract requirements for communication between
clients and servers. A concrete embedding over HTTP/2 completes the picture by
fleshing out the details of each of the required operations.


