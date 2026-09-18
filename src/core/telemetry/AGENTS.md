# Telemetry

This directory contains the implementation of gRPC's telemetry system.

## Overarching Purpose

The telemetry system provides a way to collect and report metrics and tracing about the behavior of gRPC. This information can be used for monitoring, debugging, and performance tuning.

## Files

- **`call_tracer.h` / `call_tracer.cc`**: Defines the `CallTracer` class, which is used to trace the lifecycle of a gRPC call.
- **`metrics.h` / `metrics.cc`**: Defines the `Metrics` class, which is used to collect and report metrics.
- **`stats.h` / `stats.cc`**: Defines the `Stats` class, which is used to collect and report statistics.
- **`tcp_tracer.h` / `tcp_tracer.cc`**: Defines the `TcpTracer` class, which is used to trace TCP-level events.

## Notes

- The telemetry system is a key component of gRPC's observability infrastructure.
- It provides a wealth of information about the behavior of gRPC, which can be used to diagnose and fix a wide variety of problems.
- The telemetry data can be exported to a variety of monitoring systems, such as Prometheus and OpenCensus.
