# gRPC Core Debugging & Tracing

This directory contains the implementation of the gRPC tracing library.

## Overarching Purpose

The tracing library provides a mechanism for logging trace messages from different parts of the gRPC stack. This can be useful for debugging and performance analysis. It is designed to be lightweight and should have a minimal impact on performance when it is disabled.

## How to Add a New Trace Flag

1.  Add a new entry to the `trace_flags.yaml` file. The entry should include the name of the trace flag and a brief description of what it does.
2.  Run the `tools/codegen/core/generate_trace_flags_main.cc` script to regenerate the `trace_flags.h` and `trace_flags.cc` files.
3.  Use the `GRPC_TRACE_FLAG_ENABLED` macro to check if the trace flag is enabled, and the `GRPC_TRACE_LOG`, `GRPC_TRACE_DLOG`, or `GRPC_TRACE_VLOG` macros to log trace messages.

## Files

*   **`trace.h`**: The public interface to the tracing library.
*   **`trace_impl.h`**: The implementation of the tracing library.
*   **`trace_flags.yaml`**: The definition of all of the available trace flags.

## Major Classes

*   `grpc_core::TraceFlag`: Represents a single trace flag.

## Major Macros

*   **`GRPC_TRACE_FLAG_ENABLED(tracer)`**: Checks if a trace flag is enabled. `tracer` is the name of the trace flag, without the `grpc_` prefix.
*   **`GRPC_TRACE_LOG(tracer, level)`**: Logs a trace message if a trace flag is enabled. This is equivalent to `LOG_IF(level, GRPC_TRACE_FLAG_ENABLED(tracer))`.
*   **`GRPC_TRACE_DLOG(tracer, level)`**: Logs a trace message in debug builds if a trace flag is enabled. This is equivalent to `DLOG_IF(level, GRPC_TRACE_FLAG_ENABLED(tracer))`.
*   **`GRPC_TRACE_VLOG(tracer, level)`**: Logs a verbose trace message if a trace flag is enabled. This is equivalent to `if (GRPC_TRACE_FLAG_ENABLED(tracer)) VLOG(level)`.

## Notes

*   Trace flags can be enabled at runtime by setting the `GRPC_TRACE` environment variable to a comma-separated list of trace flag names. For example, `GRPC_TRACE=api,http` will enable the `api` and `http` trace flags.
*   The special value `all` can be used to enable all trace flags.
*   Prefixing a trace flag with a `-` will disable it. For example, `GRPC_TRACE=all,-http` will enable all trace flags except for `http`.
*   The tracing library is a powerful tool for debugging gRPC. It can be used to trace the execution of an RPC, to inspect the contents of messages, and to identify performance bottlenecks.
