## Bazel Workspace Test (gRPC C++)

This directory houses a test ensuring that downstream projects can use
`@com_github_grpc_grpc//:grpc++`, `cc_proto_library`, and
`cc_grpc_library`.

To build the example (with currently checked-out version of grpc/grpc)
```
# from this directory run
bazel build //...
```

NOTE: the `greeter_client.cc`, `greeter_server.cc` and `helloword.proto`
in this directory are just copies of files from `examples/cpp/helloworld`

TODO: improve instructions in
https://github.com/grpc/grpc/tree/master/src/cpp#bazel

TODO: run the test as part of distribtests

