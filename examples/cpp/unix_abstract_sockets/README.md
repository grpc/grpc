gRPC Unix Abstract Socket Example
================

This example shows how to use gRPC with Unix domain sockets in the abstract namespace.
gRPC uses the [`unix-abstract:abstract_path`](https://github.com/grpc/grpc/blob/c6844099218b147b0e374843e0a26745adc61ddb/doc/naming.md?plain=1#L44-L50) URI scheme to support this.
In this example, an socket with an embedded null character `grpc%00abstract` is created.

## Build and run the example

Run `bazel run :server` in one terminal, and `bazel run :client` in another.

The client and server will confirm that a message was sent and received on both ends. The server will continue running until it is shut down.
While the server is still running, you can confirm that a unix domain socket is in use by running `lsof -U | grep '@grpc@abstract'`.
