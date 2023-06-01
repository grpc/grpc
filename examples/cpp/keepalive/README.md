# gRPC C++ Keepalive Example

The keepalive example builds on the [Hello World Example](https://github.com/grpc/grpc/tree/master/examples/cpp/helloworld) and changes the gRPC client and server to show a sample way of configuring keepalive pings on the client and on the server.

For more information on keepalive pings in gRPC, please refer to -
* [keepalive guide](https://github.com/grpc/grpc/blob/master/doc/keepalive.md)
* [A8: Client-side Keepalive](https://github.com/grpc/proposal/blob/master/A8-client-side-keepalive.md)
* [A9: Server-side Connection Management](https://github.com/grpc/proposal/blob/master/A9-server-side-conn-mgt.md)

## Running the example

To run the server -

```
$ tools/bazel run examples/cpp/keepalive:greeter_callback_server
```

To run the client -

```
$ tools/bazel run examples/cpp/keepalive:greeter_callback_client
```

You can find a complete set of instructions for building gRPC and running the
Hello World app in the [C++ Quick Start][].

[C++ Quick Start]: https://grpc.io/docs/languages/cpp/quickstart
