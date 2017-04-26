Service Config in gRPC
======================

# Objective

The service config is a mechanism that allows service owners to publish
parameters to be automatically used by all clients of their service.

# Format

The service config is a JSON string of the following form:

```
{
  // Load balancing policy name.
  // Supported values are 'round_robin' and 'grpclb'.
  // Optional; if unset, the default behavior is pick the first available
  // backend.
  // Note that if the resolver returns only balancer addresses and no
  // backend addresses, gRPC will always use the 'grpclb' policy,
  // regardless of what this field is set to.
  'loadBalancingPolicy': string,

  // Per-method configuration.  Optional.
  'methodConfig': [
    {
      // The names of the methods to which this method config applies. There
      // must be at least one name. Each name entry must be unique across the
      // entire service config. If the 'method' field is empty, then this
      // method config specifies the defaults for all methods for the specified
      // service.
      //
      // For example, let's say that the service config contains the following
      // method config entries:
      //
      // 'methodConfig': [
      //   { 'name': [ { 'service': 'MyService' } ] ... },
      //   { 'name': [ { 'service': 'MyService', 'method': 'Foo' } ] ... }
      // ]
      //
      // For a request for MyService/Foo, we will use the second entry, because
      // it exactly matches the service and method name.
      // For a request for MyService/Bar, we will use the first entry, because
      // it provides the default for all methods of MyService.
      'name': [
        {
          // RPC service name.  Required.
          // If using gRPC with protobuf as the IDL, then this will be of
          // the form "pkg.service_name", where "pkg" is the package name
          // defined in the proto file.
          'service': string,

          // RPC method name.  Optional (see above).
          'method': string,
        }
      ],

      // Whether RPCs sent to this method should wait until the connection is
      // ready by default. If false, the RPC will abort immediately if there
      // is a transient failure connecting to the server. Otherwise, gRPC will
      // attempt to connect until the deadline is exceeded.
      //
      // The value specified via the gRPC client API will override the value
      // set here. However, note that setting the value in the client API will
      // also affect transient errors encountered during name resolution,
      // which cannot be caught by the value here, since the service config
      // is obtained by the gRPC client via name resolution.
      'waitForReady': bool,

      // The default timeout in seconds for RPCs sent to this method. This can
      // be overridden in code. If no reply is received in the specified amount
      // of time, the request is aborted and a deadline-exceeded error status
      // is returned to the caller.
      //
      // The actual deadline used will be the minimum of the value specified
      // here and the value set by the application via the gRPC client API.
      // If either one is not set, then the other will be used.
      // If neither is set, then the request has no deadline.
      //
      // The format of the value is that of the 'Duration' type defined here:
      // https://developers.google.com/protocol-buffers/docs/proto3#json
      'timeout': string,

      // The maximum allowed payload size for an individual request or object
      // in a stream (client->server) in bytes. The size which is measured is
      // the serialized, uncompressed payload in bytes. This applies both
      // to streaming and non-streaming requests.
      //
      // The actual value used is the minimum of the value specified here and
      // the value set by the application via the gRPC client API.
      // If either one is not set, then the other will be used.
      // If neither is set, then the built-in default is used.
      //
      // If a client attempts to send an object larger than this value, it
      // will not be sent and the client will see an error.
      // Note that 0 is a valid value, meaning that the request message must
      // be empty.
      'maxRequestMessageBytes': number,

      // The maximum allowed payload size for an individual response or object
      // in a stream (server->client) in bytes. The size which is measured is
      // the serialized, uncompressed payload in bytes. This applies both
      // to streaming and non-streaming requests.
      //
      // The actual value used is the minimum of the value specified here and
      // the value set by the application via the gRPC client API.
      // If either one is not set, then the other will be used.
      // If neither is set, then the built-in default is used.
      //
      // If a server attempts to send an object larger than this value, it
      // will not be sent, and the client will see an error.
      // Note that 0 is a valid value, meaning that the response message must
      // be empty.
      'maxResponseMessageBytes': number
    }
  ]
}
```

Note that new per-method parameters may be added in the future as new
functionality is introduced.

# Architecture

A service config is associated with a server name.  The [name
resolver](naming.md) plugin, when asked to resolve a particular server
name, will return both the resolved addresses and the service config.

TODO(roth): Design how the service config will be encoded in DNS.

# APIs

The service config is used in the following APIs:

- In the resolver API, used by resolver plugins to return the service
  config to the gRPC client.
- In the gRPC client API, where users can query the channel to obtain
  the service config associated with the channel (for debugging
  purposes).
- In the gRPC client API, where users can set the service config
  explicitly.  This is intended for use in unit tests.
