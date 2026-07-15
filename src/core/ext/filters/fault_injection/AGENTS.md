# Fault Injection Filter

This directory contains a client-side filter for injecting faults into gRPC calls.

## Overarching Purpose

The fault injection filter provides a mechanism for testing the resilience of a gRPC application by programmatically introducing delays and aborting requests. This can be used to simulate various failure scenarios, such as network latency or server errors.

## Files

*   `fault_injection_filter.h`, `fault_injection_filter.cc`: These files define the `FaultInjectionFilter` class, a client-side filter that can inject delays and aborts into outgoing requests. The behavior of the filter is controlled by a fault injection policy, which is configured via xDS.

## Major Classes

*   `grpc_core::FaultInjectionFilter`: The channel filter implementation.

## Notes

*   The filter limits the number of concurrent faults to a configured maximum, which can be useful for preventing fault injection from overwhelming the system.
*   This filter is a powerful tool for reliability testing and can help to identify and fix potential issues in a gRPC application before they occur in production.
