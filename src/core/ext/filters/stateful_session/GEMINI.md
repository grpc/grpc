# Stateful Session Filter

This directory contains a client-side filter for providing cookie-based stateful session affinity.

## Overarching Purpose

The stateful session filter provides a mechanism for routing requests to the same backend endpoint that handled previous requests from the same client. This is useful for applications that require session affinity, such as those that use in-memory caches or other stateful resources.

## Files

*   `stateful_session_filter.h`, `stateful_session_filter.cc`: These files define the `StatefulSessionFilter` class, a client-side filter that provides cookie-based stateful session affinity.
*   `stateful_session_service_config_parser.h`, `stateful_session_service_config_parser.cc`: These files define the `StatefulSessionServiceConfigParser` class, which is responsible for parsing the stateful session configuration from the service config.

## Major Classes

*   `grpc_core::StatefulSessionFilter`: The channel filter implementation.
*   `grpc_core::XdsOverrideHostAttribute`: A call attribute that is used to pass state between the `StatefulSessionFilter` and the `xds_override_host` load balancing policy.
*   `grpc_core::StatefulSessionServiceConfigParser`: The service config parser for the stateful session configuration.

## Notes

*   The stateful session filter works in conjunction with the `xds_override_host` load balancing policy. The filter extracts a cookie from the incoming request's metadata, which the LB policy then uses to select an endpoint.
*   The filter is designed to be used with xDS, which can be used to dynamically update the stateful session configuration at runtime.
*   This filter is an important component of gRPC's load balancing story, as it enables applications to maintain session affinity across multiple requests.
