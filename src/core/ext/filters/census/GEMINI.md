# Census Integration

This directory contains minimal, legacy functionality for integrating with the
now-deprecated Census library.



## Overarching Purpose

This code provides a way to attach a `census_context` to a `grpc_call`,
allowing for the propagation of tracing and stats information.

## Files

*   `grpc_context.cc`: Implements `grpc_census_call_set_context` and
    `grpc_census_call_get_context`, which are used to attach and retrieve a
    `census_context` from a `grpc_call`'s arena.

## Notes

*   This integration is considered obsolete. OpenCensus and OpenTelemetry are
    the modern, recommended ways to instrument gRPC.
