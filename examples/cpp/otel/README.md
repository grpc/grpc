# gRPC C++ OpenTelemetry Example

The opentelemetry example builds on the
[Hello World Example](https://github.com/grpc/grpc/tree/master/examples/cpp/helloworld)
and changes the gRPC client and server to show a sample way of configuring the
gRPC OpenTelemetry plugin with a prometheus exporter.

For more information on the gRPC OpenTelemetry plugin, please refer to - *
[A66: OpenTelemetry Metrics](https://github.com/grpc/proposal/blob/master/A66-otel-stats.md)
* [https://opentelemetry.io/]()

## Running the example

To run the server -

```
$ tools/bazel run examples/cpp/otel:greeter_callback_server
```

To run the client -

```
$ tools/bazel run examples/cpp/otel:greeter_callback_client
```

The client continuously sends an RPC to the server every second.

To make sure that the server and client metrics are being exported properly, in
a separate terminal, run the following -

```
$ curl localhost:9464/metrics
```

```
$ curl localhost:9465/metrics
```

> ***NOTE:*** If the prometheus endpoint configured is overridden, please update
> the target in the above curl command.

## CMake Instructions

The following libraries need to be installed before building the example with CMake -
* absl
* protobuf
* prometheus-cpp
* opentelemetry-cpp (with the options `-DWITH_ABSEIL=ON` `-DWITH_PROMETHEUS=ON`)
* grpc (with the option `-DgRPC_BUILD_GRPCPP_OTEL_PLUGIN=ON`)

You can find a complete set of instructions for building gRPC and running the
Hello World app in the [C++ Quick Start][].

[C++ Quick Start]: https://grpc.io/docs/languages/cpp/quickstart
