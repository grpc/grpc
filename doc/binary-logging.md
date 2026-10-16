# Binary Logging

## Format

The log format is described in [this proto file](https://github.com/grpc/grpc-proto/blob/master/grpc/binlog/v1/binarylog.proto). It is intended that multiple parts of the call will be logged in separate files, and then correlated by analysis tools using the rpc\_id.

## API

The binary logger will be a separate library from gRPC, in each language that we support. The user will need to explicitly call into the library to generate logs. The library will provide the ability to log sending or receiving, as relevant, the following on both the client and the server:

 - Initial metadata
 - Messages
 - Status with trailing metadata from the server
 - Additional key/value pairs that are associated with a call but not sent over the wire

The following is an example of what such an API could look like in C++:

```c++
// The context provides the method_name, deadline, peer, and metadata contents.
// direction = CLIENT_SEND
LogRequestHeaders(ClientContext context);
// direction = SERVER_RECV
LogRequestHeaders(ServerContext context);

// The context provides the metadata contents
// direction = CLIENT_RECV
LogResponseHeaders(ClientContext context);
// direction = SERVER_SEND
LogResponseHeaders(ServerContext context);

// The context provides the metadata contents
// direction = CLIENT_RECV
LogStatus(ClientContext context, grpc_status_code code, string details);
// direction = SERVER_SEND
LogStatus(ServerContext context, grpc_status_code code, string details);

// The context provides the user data contents
// direction = CLIENT_SEND
LogUserData(ClientContext context);
// direction = SERVER_SEND
LogUserData(ServerContext context);

// direction = CLIENT_SEND
LogRequestMessage(ClientContext context, uint32_t length, T message);
// direction = SERVER_RECV
LogRequestMessage(ServerContext context, uint32_t length, T message);
// direction = CLIENT_RECV
LogResponseMessage(ClientContext context, uint32_t length, T message);
// direction = SERVER_SEND
LogResponseMessage(ServerContext context, uint32_t length, T message);
```

In all of those cases, the `rpc_id` is provided by the context, and each combination of method and context argument type implies a single direction, as noted in the comments.

For the message log functions, the `length` argument indicates the length of the complete message, and the `message` argument may be only part of the complete message, stripped of sensitive material and/or shortened for efficiency.

## Language differences

In other languages, more or less data will need to be passed explicitly as separate arguments. In some languages, for example, the metadata will be separate from the context-like object and will need to be passed as a separate argument.
