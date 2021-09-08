# Status codes and their use in gRPC

gRPC uses a set of well defined status codes as part of the RPC API. These
statuses are defined as such:

| Code | Number | Description |
|------------------|--------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| OK | 0 | Not an error; returned on success. |
| CANCELLED | 1 | The operation was cancelled, typically by the caller. |
| UNKNOWN | 2 | Unknown error. For example, this error may be returned when a `Status` value received from another address space belongs to an error space that is not known in this address space. Also errors raised by APIs that do not return enough error information may be converted to this error. |
| INVALID_ARGUMENT | 3 | The client specified an invalid argument. Note that this differs from `FAILED_PRECONDITION`. `INVALID_ARGUMENT` indicates arguments that are problematic regardless of the state of the system (e.g., a malformed file name). |
| DEADLINE_EXCEEDED | 4 | The deadline expired before the operation could complete. For operations that change the state of the system, this error may be returned even if the operation has completed successfully. For example, a successful response from a server could have been delayed long |
| NOT_FOUND | 5 | Some requested entity (e.g., file or directory) was not found. Note to server developers: if a request is denied for an entire class of users, such as gradual feature rollout or undocumented allowlist, `NOT_FOUND` may be used. If a request is denied for some users within a class of users, such as user-based access control, `PERMISSION_DENIED` must be used. |
| ALREADY_EXISTS | 6 | The entity that a client attempted to create (e.g., file or directory) already exists. |
| PERMISSION_DENIED | 7 | The caller does not have permission to execute the specified operation. `PERMISSION_DENIED` must not be used for rejections caused by exhausting some resource (use `RESOURCE_EXHAUSTED` instead for those errors). `PERMISSION_DENIED` must not be used if the caller can not be identified (use `UNAUTHENTICATED` instead for those errors). This error code does not imply the request is valid or the requested entity exists or satisfies other pre-conditions. |
| RESOURCE_EXHAUSTED | 8 | Some resource has been exhausted, perhaps a per-user quota, or perhaps the entire file system is out of space. |
| FAILED_PRECONDITION | 9 | The operation was rejected because the system is not in a state required for the operation's execution. For example, the directory to be deleted is non-empty, an rmdir operation is applied to a non-directory, etc. Service implementors can use the following guidelines to decide between `FAILED_PRECONDITION`, `ABORTED`, and `UNAVAILABLE`: (a) Use `UNAVAILABLE` if the client can retry just the failing call. (b) Use `ABORTED` if the client should retry at a higher level (e.g., when a client-specified test-and-set fails, indicating the client should restart a read-modify-write sequence). (c) Use `FAILED_PRECONDITION` if the client should not retry until the system state has been explicitly fixed. E.g., if an "rmdir" fails because the directory is non-empty, `FAILED_PRECONDITION` should be returned since the client should not retry unless the files are deleted from the directory. |
| ABORTED | 10 | The operation was aborted, typically due to a concurrency issue such as a sequencer check failure or transaction abort. See the guidelines above for deciding between `FAILED_PRECONDITION`, `ABORTED`, and `UNAVAILABLE`. |
| OUT_OF_RANGE | 11 | The operation was attempted past the valid range. E.g., seeking or reading past end-of-file. Unlike `INVALID_ARGUMENT`, this error indicates a problem that may be fixed if the system state changes. For example, a 32-bit file system will generate `INVALID_ARGUMENT` if asked to read at an offset that is not in the range [0,2^32-1], but it will generate `OUT_OF_RANGE` if asked to read from an offset past the current file size. There is a fair bit of overlap between `FAILED_PRECONDITION` and `OUT_OF_RANGE`. We recommend using `OUT_OF_RANGE` (the more specific error) when it applies so that callers who are iterating through a space can easily look for an `OUT_OF_RANGE` error to detect when they are done. |
| UNIMPLEMENTED | 12 | The operation is not implemented or is not supported/enabled in this service. |
| INTERNAL | 13 | Internal errors. This means that some invariants expected by the underlying system have been broken. This error code is reserved for serious errors. |
| UNAVAILABLE | 14 | The service is currently unavailable. This is most likely a transient condition, which can be corrected by retrying with a backoff. Note that it is not always safe to retry non-idempotent operations. |
| DATA_LOSS | 15 | Unrecoverable data loss or corruption. |
| UNAUTHENTICATED | 16 | The request does not have valid authentication credentials for the operation. |

All RPCs started at a client return a `status` object composed of an integer
`code` and a string `message`. The server-side can choose the status it
returns for a given RPC.

The gRPC client and server-side implementations may also generate and
return `status` on their own when errors happen. Only a subset of
the pre-defined status codes are generated by the gRPC libraries. This
allows applications to be sure that any other code it sees was actually
returned by the application (although it is also possible for the
server-side to return one of the codes generated by the gRPC libraries).

The following table lists the codes that may be returned by the gRPC
libraries (on either the client-side or server-side) and summarizes the
situations in which they are generated.

| Case        | Code           | Generated at Client or Server  |
| ------------- |:-------------| :-----:|
| Client Application cancelled the request	| CANCELLED | Both |
| Deadline expires before server returns status	| DEADLINE_EXCEEDED | Both |
| Method not found at server	| UNIMPLEMENTED | Server|
| Server shutting down	| UNAVAILABLE | Server|
| Server side application throws an exception (or does something other than returning a Status code to terminate an RPC) |	UNKNOWN | Server|
| No response received before Deadline expires. This may occur either when the client is unable to send the request to the server or when the server fails to respond in time. |	DEADLINE_EXCEEDED | Both|
| Some data transmitted (e.g., request metadata written to TCP connection) before connection breaks |	UNAVAILABLE | Client |
| Could not decompress, but compression algorithm supported (Client -> Server)	| INTERNAL | Server |
| Could not decompress, but compression algorithm supported (Server -> Client)	| INTERNAL | Client |
| Compression mechanism used by client not supported at server	| UNIMPLEMENTED | Server |
| Server temporarily out of resources (e.g., Flow-control resource limits reached) |	RESOURCE_EXHAUSTED | Server|
| Client does not have enough memory to hold the server response | RESOURCE_EXHAUSTED | Client |
| Flow-control protocol violation |	INTERNAL | Both |
| Error parsing returned status	| UNKNOWN | Client |
| Incorrect Auth metadata ( Credentials failed to get metadata, Incompatible credentials set on channel and call, Invalid host set in `:authority` metadata, etc.) | UNAUTHENTICATED | Both |
| Request cardinality violation (method requires exactly one request but client sent some other number of requests) | UNIMPLEMENTED | Server|
| Response cardinality violation (method requires exactly one response but server sent some other number of responses) | UNIMPLEMENTED | Client|
| Error parsing response proto	| INTERNAL | Client|
| Error parsing request proto	| INTERNAL | Server|
| Sent or received message was larger than configured limit | RESOURCE_EXHAUSTED | Both |
| Keepalive watchdog times out | UNAVAILABLE | Both |

The following status codes are never generated by the library:
- INVALID_ARGUMENT
- NOT_FOUND
- ALREADY_EXISTS
- FAILED_PRECONDITION
- ABORTED
- OUT_OF_RANGE
- DATA_LOSS

Applications that may wish to [retry](https://github.com/grpc/proposal/blob/master/A6-client-retries.md) failed RPCs must decide which status codes on which to retry. As shown in the table above, the gRPC library can generate the same status code for different cases. Server applications can also return those same status codes. Therefore, there is no fixed list of status codes on which it is appropriate to retry in all applications. As a result, individual applications must make their own determination as to which status codes should cause an RPC to be retried.
