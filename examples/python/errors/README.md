# gRPC Python Error Handling Example

The goal of this example is sending error status from server that is more complicated than a code and detail string.

The definition for an RPC method in proto files contains request message and response message. There are many error states that can be shared across RPC methods (e.g. stack trace, insufficient quota). Using a different path to handle error will make the code more maintainable.

Ideally, the final status of an RPC should be described in the trailing headers of HTTP2, and gRPC Python provides helper functions in `grpcio-status` package to assist the packing and unpacking of error status.


### Requirement
```
grpcio>=1.18.0
grpcio-status>=1.18.0
googleapis-common-protos>=1.5.5
```


### Error Detail Proto

You may provide any custom proto message as error detail in your implementation. Here are protos are defined by Google Cloud Library Team:

* [code.proto]([https://github.com/googleapis/api-common-protos/blob/master/google/rpc/code.proto](https://github.com/googleapis/api-common-protos/blob/87185dfffad4afa5a33a8c153f0e1ea53b4f85dc/google/rpc/code.proto)) contains definition of RPC error codes.
* [error_details.proto]([https://github.com/googleapis/api-common-protos/blob/master/google/rpc/error_details.proto](https://github.com/googleapis/api-common-protos/blob/87185dfffad4afa5a33a8c153f0e1ea53b4f85dc/google/rpc/error_details.proto)) contains definitions of common error details.


### Definition of Status Proto

Here is the definition of Status proto. For full text, please see [status.proto](https://github.com/googleapis/api-common-protos/blob/87185dfffad4afa5a33a8c153f0e1ea53b4f85dc/google/rpc/status.proto).

```proto
// The `Status` type defines a logical error model that is suitable for different
// programming environments, including REST APIs and RPC APIs. It is used by
// [gRPC](https://github.com/grpc). The error model is designed to be:
//
// - Simple to use and understand for most users
// - Flexible enough to meet unexpected needs
//
// # Overview
//
// The `Status` message contains three pieces of data: error code, error message,
// and error details. The error code should be an enum value of
// [google.rpc.Code][google.rpc.Code], but it may accept additional error codes if needed.  The
// error message should be a developer-facing English message that helps
// developers *understand* and *resolve* the error. If a localized user-facing
// error message is needed, put the localized message in the error details or
// localize it in the client. The optional error details may contain arbitrary
// information about the error. There is a predefined set of error detail types
// in the package `google.rpc` that can be used for common error conditions.
//
// # Language mapping
//
// The `Status` message is the logical representation of the error model, but it
// is not necessarily the actual wire format. When the `Status` message is
// exposed in different client libraries and different wire protocols, it can be
// mapped differently. For example, it will likely be mapped to some exceptions
// in Java, but more likely mapped to some error codes in C.
//
// # Other uses
//
// The error model and the `Status` message can be used in a variety of
// environments, either with or without APIs, to provide a
// consistent developer experience across different environments.
//
// Example uses of this error model include:
//
// - Partial errors. If a service needs to return partial errors to the client,
//     it may embed the `Status` in the normal response to indicate the partial
//     errors.
//
// - Workflow errors. A typical workflow has multiple steps. Each step may
//     have a `Status` message for error reporting.
//
// - Batch operations. If a client uses batch request and batch response, the
//     `Status` message should be used directly inside batch response, one for
//     each error sub-response.
//
// - Asynchronous operations. If an API call embeds asynchronous operation
//     results in its response, the status of those operations should be
//     represented directly using the `Status` message.
//
// - Logging. If some API errors are stored in logs, the message `Status` could
//     be used directly after any stripping needed for security/privacy reasons.
message Status {
  // The status code, which should be an enum value of [google.rpc.Code][google.rpc.Code].
  int32 code = 1;

  // A developer-facing error message, which should be in English. Any
  // user-facing error message should be localized and sent in the
  // [google.rpc.Status.details][google.rpc.Status.details] field, or localized by the client.
  string message = 2;

  // A list of messages that carry the error details.  There is a common set of
  // message types for APIs to use.
  repeated google.protobuf.Any details = 3;
}
```


### Usage of Well-Known-Proto `Any`

Please check [ProtoBuf Document: Any](https://developers.google.com/protocol-buffers/docs/reference/python-generated#any)

```Python
any_message.Pack(message)
any_message.Unpack(message)
assert any_message.Is(message.DESCRIPTOR)
```
