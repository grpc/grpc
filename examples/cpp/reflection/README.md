gRPC Reflection Example
================

This example shows how reflection can be registered on a gRPC server.

### Build and run the example

To start the reflection server on its default port of 50051, run the following command from within the `examples/cpp/reflection` folder:

```
$ bazel run :reflection_server
```

There are multiple existing reflection clients you can use to inspect its services.

To use `gRPC CLI`, see https://github.com/grpc/grpc-go/blob/master/Documentation/server-reflection-tutorial.md#grpc-cli.

To use `grpcurl`, see https://github.com/fullstorydev/grpcurl.
