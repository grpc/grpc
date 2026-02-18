# Backend Metrics Filter

This directory contains a server-side filter for reporting backend metrics.

## Overarching Purpose

The filter provides a mechanism to collect backend metric data, serialize it, and send it as trailing metadata in a gRPC call. This data can be used by clients to make load balancing decisions.

## Files

*   `backend_metric_filter.h`, `backend_metric_filter.cc`: These files define the `BackendMetricFilter` class, a server-side channel filter that retrieves backend metrics from a `BackendMetricProvider`, serializes them using the `xds.data.orca.v3.OrcaLoadReport` protobuf message, and attaches the result to the server's trailing metadata. The filter is activated by the `GRPC_ARG_SERVER_CALL_METRIC_RECORDING` channel argument.
*   `backend_metric_provider.h`: This file defines the `BackendMetricProvider` interface, which is responsible for providing the backend metric data to the filter.

## Major Classes

*   `grpc_core::BackendMetricFilter`: The channel filter implementation.
*   `grpc_core::BackendMetricProvider`: An interface for a class that can provide backend metric data.

## Notes

*   This filter is a key component of gRPC's custom load balancing support. It allows backends to provide real-time load information to clients, enabling more intelligent load balancing decisions.
*   The filter is configured to run on the server and is only active when the appropriate channel argument is set.
