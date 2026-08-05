# OpenTelemetry Interop Test Infrastructure

## Overview
The OpenTelemetry interop test suite (`test/cpp/interop/run_otel_interop_test.py`) verifies OpenTelemetry context propagation, span creation, and metric collection across a 4x4 matrix of gRPC implementations:
- **C++** (`//test/cpp/interop:interop_client`, `//test/cpp/interop:interop_server`)
- **Java** (`grpc-java/interop-testing`)
- **Python** (`//src/python/grpcio_tests/tests/interop:client`, `//src/python/grpcio_tests/tests/interop:server_bin`)
- **Go** (`grpc-go/interop/client`, `grpc-go/interop/server`)

## Architecture & Components
1. **OTLP Collector**: `//test/cpp/interop:otlp_collector` runs locally in the background, listening on an ephemeral gRPC port for OTLP trace and metric exports. It dumps captured spans to `captured_spans.json` and metrics to `captured_metrics.json`.
2. **Interop Servers**: Started with OTLP exporter environment variables and flags (`--enable_opentelemetry=true`, `--enable_tcp_metrics=true`).
3. **Interop Clients**: Executed against the running server for `empty_unary` test cases.
4. **Verifications**:
   - `verify_spans()`: Validates that 3 spans exist (Client `Sent`, Client `Attempt`, Server `Recv`), all share the same `TraceID`, follow proper parent-child hierarchy, contain expected attributes (`previous-rpc-attempts=0`, `transparent-retry=False`), and include message events.
   - `verify_metrics()`: Validates metric presence and, for C++ servers, asserts `grpc.tcp.*` metrics (min_rtt, bytes_sent, connections_created, connection_count) and label keys (`network.local.address`, `network.local.port`, `network.peer.address`, `network.peer.port`, `is_client`). For non-C++ servers, TCP metric assertions are safely bypassed.

## Execution
Run the full matrix or specific client/server pair:
```bash
python3 test/cpp/interop/run_otel_interop_test.py --client=<c++|java|python|go> --server=<c++|java|python|go>
```
To skip rebuilds of already compiled binaries:
```bash
python3 test/cpp/interop/run_otel_interop_test.py --client=<c++|java|python|go> --server=<c++|java|python|go> --skip_build
```
