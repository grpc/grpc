# HTTP Client Filter

This directory contains a client-side filter for setting HTTP/2 pseudo-headers.

See also: [gRPC HTTP Filters overview](../GEMINI.md)

## Overarching Purpose

The HTTP client filter is responsible for setting various HTTP/2 pseudo-headers that are required for gRPC calls to function correctly. It also performs some basic validation of the server's response headers.

## Files

*   `http_client_filter.h`, `http_client_filter.cc`: These files define the `HttpClientFilter` class, a client-side filter that sets the `:method`, `:scheme`, `te`, `content-type`, and `user-agent` headers on outgoing requests.

## Major Classes

*   `grpc_core::HttpClientFilter`: The channel filter implementation.

## Notes

*   This filter is an essential part of the gRPC client-side channel stack. Without it, gRPC calls would not be compliant with the HTTP/2 specification.
*   The filter's behavior can be customized using various channel arguments, such as those for setting the user agent and scheme.
*   The `GRPC_ARG_TEST_ONLY_USE_PUT_REQUESTS` channel argument can be used to force the filter to use `PUT` requests instead of `POST`. This is intended for testing purposes only, and should not be used in production.
