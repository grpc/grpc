# HTTP Server Filter

This directory contains a server-side filter for processing HTTP/2 headers.

See also: [gRPC HTTP Filters overview](../GEMINI.md)

## Overarching Purpose

The HTTP server filter is a critical component of the gRPC server-side channel stack. It is responsible for processing incoming HTTP/2 headers to ensure that they are valid for a gRPC request, as defined by the [gRPC-over-HTTP/2 specification](https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md).

## Files

*   `http_server_filter.h`, `http_server_filter.cc`: These files define the `HttpServerFilter` class, a server-side filter that validates incoming HTTP/2 headers and manipulates metadata.

## Header Validation

The filter performs the following validation checks on the incoming headers:

*   It ensures that the `:scheme` header is `http` or `https`.
*   It ensures that the `:method` header is `POST`. It can be configured to allow `PUT` requests for testing purposes, but this is not recommended for production use.
*   It ensures that the `content-type` header is `application/grpc` or a variation thereof.
*   It ensures that the `te` header is `trailers`.

If any of these checks fail, the filter will send an error to the client.

## Header Manipulation

The filter also performs the following header manipulations:

*   It can be configured to surface the `user-agent` header to the application.

## Major Classes

*   `grpc_core::HttpServerFilter`: The channel filter implementation.

## Notes

*   This filter is an essential part of the gRPC server-side channel stack. It ensures that incoming requests are compliant with the gRPC-over-HTTP/2 specification.
*   The `GRPC_ARG_DO_NOT_USE_UNLESS_YOU_HAVE_PERMISSION_FROM_GRPC_TEAM_ALLOW_BROKEN_PUT_REQUESTS` channel argument can be used to allow `PUT` requests. This is intended for testing purposes only, and should not be used in production. `PUT` requests are not compliant with the gRPC specification, and can cause problems with some gRPC features, such as retries.
*   The filter also has an option to surface the `user-agent` header to the application, which can be useful for analytics or other purposes. This is controlled by the `GRPC_ARG_SURFACE_USER_AGENT` channel argument.
