# HTTP Filters

This directory contains various HTTP-related channel filters and functionality.



## Overarching Purpose

This directory contains a collection of filters that are responsible for handling HTTP/2-specific functionality in gRPC. This includes things like header manipulation, message compression, and authority checking.

## Subdirectories

*   **[`client`](./client/GEMINI.md)**: Contains client-side HTTP filters.
*   **[`server`](./server/GEMINI.md)**: Contains server-side HTTP filters.

## Files

*   **`client_authority_filter.h`, `client_authority_filter.cc`**: These files contain the implementation of the `ClientAuthorityFilter`, a client-side filter that sets the `:authority` pseudo-header in HTTP/2 requests.
*   **`http_filters_plugin.cc`**: This file registers the HTTP filters with the `CoreConfiguration`.

## Notes

*   The filters in this directory are essential for ensuring that gRPC is compliant with the HTTP/2 specification.
*   The `ClientAuthorityFilter` is a good example of a simple, single-purpose filter. It has one job (to set the `:authority` header), and it does it well.
*   The `message_compress` filter is a good example of a more complex filter that is used on both the client and the server. It is responsible for compressing and decompressing messages, and it supports a variety of different compression algorithms.
