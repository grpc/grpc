# HTTP to gRPC Status Code Mapping

Since intermediaries are a common part of HTTP infrastructure some responses to
gRPC requests may be received that do not include the grpc-status header. In
some cases mapping error codes from an intermediary allows the gRPC client to
behave more appropriately to the error situation without overloading the
semantics of either error code.

This table is to be used _only_ for clients that received a response that did
not include grpc-status. If grpc-status was provided, it _must_ be used. Servers
_must not_ use this table to determine an HTTP status code to use; the mappings
are neither symmetric nor 1-to-1.

| HTTP Status Code           | gRPC Status Code   |
|----------------------------|--------------------|
| 400 Bad Request            | INTERNAL           |
| 401 Unauthorized           | UNAUTHENTICATED    |
| 403 Forbidden              | PERMISSION\_DENIED |
| 404 Not Found              | UNIMPLEMENTED      |
| 429 Too Many Requests      | UNAVAILABLE        |
| 502 Bad Gateway            | UNAVAILABLE        |
| 503 Service Unavailable    | UNAVAILABLE        |
| 504 Gateway Timeout        | UNAVAILABLE        |
| _All other codes_          | UNKNOWN            |

Technically, 1xx should have the entire header skipped and a subsequent header
be read. See RFC 7540 §8.1.

200 is UNKNOWN because there should be a grpc-status in case of truly OK
response.
