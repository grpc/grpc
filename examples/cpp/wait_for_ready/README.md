# gRPC C++ Wait-For-Ready Example

The Wait-For-Ready example builds on the
[Hello World Example](https://github.com/grpc/grpc/tree/master/examples/cpp/helloworld)
and changes the gRPC client and server to show how to set wait-for-ready.

For more information on wait-for-ready in gRPC, please refer to
[gRPC Wait For Ready Semantics](https://github.com/grpc/grpc/blob/master/doc/wait-for-ready.md).

## Running the example

First run the client -

```
$ tools/bazel run examples/cpp/wait_for_ready:greeter_callback_client
```

On running this, we'll see 10 RPCs failed due to "Connection refused" errors.
These RPCs did not have WAIT_FOR_READY set, resulting in the RPCs not waiting
for the channel to be connected.

The next 10 RPCs have WAIT_FOR_READY set, so the client will be waiting for the
channel to be ready before progressing.

Now, on a separate terminal, run the server -

```
$ tools/bazel run examples/cpp/helloworld:greeter_callback_server
```

The client channel should now be able to connect to the server, and the RPCs
should succeed.
