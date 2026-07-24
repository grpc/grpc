gRPC Observability Example
=====================

The examples here demonstrate how to setup gRPC Python Observability with OpenTelemetry.

More details about how to use gRPC Python Observability APIs can be found in [OpenTelemetry Metrics gRFC](https://github.com/grpc/proposal/blob/master/A66-otel-stats.md#opentelemetry-metrics) and the [OpenTelemetry Tracing gRFC](https://github.com/grpc/proposal/blob/master/A72-open-telemetry-tracing.md).

This directory contains both synchronous and asynchronous (AsyncIO) examples of gRPC observability metrics and tracing.

### Install Requirements

1. Navigate to this directory:

```sh
cd examples/python/observability
```

2. Install requirements:

```sh
python -m pip install -r requirements.txt
```

### Synchronous Example

#### Run the Server

Start the server:

```sh
python -m observability_greeter_server
```

#### Run the Client

Note that client should start within 10 seconds of the server becoming active.

```sh
python -m observability_greeter_client
```

### AsyncIO Example

#### Run the Server

Start the server:

```sh
python -m async_observability_greeter_server
```

#### Run the Client

Note that client should start within 10 seconds of the server becoming active.

```sh
python -m async_observability_greeter_client
```

### Verifying Metrics

Both synchronous and asynchronous examples will print a list of metric names collected.

Server Side:

```
Server started, listening on 50051
Metrics exported on Server side:
grpc.server.call.started
grpc.server.call.sent_total_compressed_message_size
grpc.server.call.rcvd_total_compressed_message_size
grpc.server.call.duration
```

Client Side:

```
Greeter client received: Hello You
Metrics exported on client side:
grpc.client.call.duration
grpc.client.attempt.started
grpc.client.attempt.sent_total_compressed_message_size
grpc.client.attempt.rcvd_total_compressed_message_size
grpc.client.attempt.duration
```

### Tracing example

The tracing example enabled OpenTelemetry tracing on both the gRPC clinet and
server, exporting finished spans to the console with OpenTelemetry's
`ConsoleSpanExporter`.

#### Run the Server

```sh
python -m tracing_greeter_server
```

#### Run the Client

Note that client should start within 10 seconds of the server becoming active.

```sh
python -m tracing_greeter_client
```

### Verifying Traces

The client prints the per call `Sent.*` span and the per attempt `Attempt.*`
span. Both are children of the application's `greeter-client-call` span, and
each attempt carries `Outbound message` / `Inbound message` events:

```
{
    "name": "Attempt.helloworld.Greeter/SayHello",
    ...
}
{
    "name": "Sent.helloworld.Greeter/SayHello",
    ...
}
```

The server prints the matching `Recv.*` span. Because both sides are configured
with a text map propagator, all spans share a single trace id.

```
{
    "name": "Recv.helloworld.Greeter/SayHello",
    ...
}
```
