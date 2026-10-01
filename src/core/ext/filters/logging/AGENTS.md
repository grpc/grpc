# Logging Filter

This directory contains a gRPC filter that logs call data.

## Overarching Purpose

The logging filter provides a way to log information about gRPC calls, such as the method name, the peer address, and the status of the call. This can be useful for debugging and for generating audit logs.

## Files

- **`logging_filter.h` / `logging_filter.cc`**: The implementation of the logging filter.
- **`logging_sink.h`**: Defines the `LoggingSink` interface, which is used to output log messages.

## Notes

- The logging filter is a simple but powerful tool for understanding the behavior of gRPC applications.
- It can be configured to log different levels of detail, and the output can be sent to a variety of different sinks.
