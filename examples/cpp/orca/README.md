# gRPC Custom Metrics Example

You can find a complete set of instructions for building gRPC and running the
examples in the [C++ Quick Start][].

This example shows how to implement a server that provides custom metrics usable
by custom load balancing policies.

Server needs to be setup with metrics recorder and Orca service for sending
these metrics to a client:

```c++
GreeterServiceImpl service;
// Setup custom metrics recording
auto server_metric_recorder =
    grpc::experimental::ServerMetricRecorder::Create();
grpc::experimental::OrcaService orca_service(
    server_metric_recorder.get(),
    grpc::experimental::OrcaService::Options().set_min_report_duration(
        absl::Seconds(0.1)));
builder.RegisterService(&orca_service);
grpc::ServerBuilder::experimental_type(&builder).EnableCallMetricRecording(
    nullptr);
```

Afterwards per-request metrics can be reported from the gRPC service
implementation using the metric recorder from the request context:

```c++
auto recorder = context->ExperimentalGetCallMetricRecorder();
if (recorder == nullptr) {
  return Status(grpc::StatusCode::INTERNAL,
                "Unable to access metrics recorder. Make sure "
                "EnableCallMetricRecording had been called.");
}
recorder->RecordCpuUtilizationMetric(0.5);
```

Out of band metrics can be reported using the `server_metric_recorder`
directly:

```c++
server_metric_recorder->SetCpuUtilization(0.75);
```

[C++ Quick Start]: https://grpc.io/docs/languages/cpp/quickstart
