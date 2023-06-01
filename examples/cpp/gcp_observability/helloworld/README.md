# gRPC C++ GCP Observability Hello World Example

This example consists of a hello world client and a hello world server
instrumented with GCP Observability for logs, metrics and tracing. Note that
familiarity with the [basic hello world][] example is assumed.

Please refer to GCP's Microservices Observability user guide for setup
instructions.

[basic hello world]: https://grpc.io/docs/languages/cpp/quickstart

### Run the example with configuration

To use Observability, you should first setup and configure authorization as
mentioned in the Microservices Observability user guide.

You need to set the `GRPC_GCP_OBSERVABILITY_CONFIG_FILE` environment variable to
point to the gRPC GCP Observability configuration file (preferred) or
alternatively set `GRPC_GCP_OBSERVABILITY_CONFIG` environment variable to gRPC
GCP Observability configuration value. This is needed by both client and server.

Sample configurations are provided with the example.

To start the observability-enabled example server on its default port of 50051,
run the following from the `grpc` directory:

```
$ export
    GRPC_GCP_OBSERVABILITY_CONFIG_FILE="$(pwd)/examples/cpp/gcp_observability/helloworld/server_config.json"
$ tools/bazel run examples/cpp/gcp_observability/helloworld:greeter_server
```

In a different terminal window, run the observability-enabled example client:

```
$ export
    GRPC_GCP_OBSERVABILITY_CONFIG_FILE="$(pwd)/examples/cpp/gcp_observability/helloworld/client_config.json"
$ tools/bazel run examples/cpp/gcp_observability/helloworld:greeter_client
```
