Service Config in gRPC
======================

# Objective

The service config is a mechanism that allows service owners to publish
parameters to be automatically used by all clients of their service.

# Format

The fields of the service config are defined by the
[`grpc.service_config.ServiceConfig` protocol buffer
message](https://github.com/grpc/grpc-proto/blob/master/grpc/service_config/service_config.proto).
Note that new fields may be added in the future as new functionality is
introduced.

Internally, gRPC uses the service config in JSON form.  The JSON
representation is the result of converting the protobuf form into JSON
using the normal [protobuf to JSON translation
rules](https://developers.google.com/protocol-buffers/docs/proto3#json).
In particular, this means:
- Field names are converted from `snake_case` to `camelCase`.
- Field values are converted as per the documented translation rules:
  - Strings, 32-bit integers, and bools are converted into the
    corresponding JSON types.
  - 64-bit integers are converted into strings (e.g., `"251"`).
  - The value of a repeated field will be represented as a JSON array.
  - The value of a `google.protobuf.Duration` will be represented as a
    string containing a decimal number of seconds (e.g., `"1.000340012s"`).

For more details, see the protobuf docs linked above.

Note that the JSON representation has one advantage over the protobuf
representation, which is that it is possible to encode configurations
for [LB policies](load-balancing.md) that are not known to gRPC.  In
protobuf form, the `loadBalancingConfig` field contains a `oneof`
supporting only the built-in LB policies.  However, in JSON form, the
field inside the `oneof` is encoded as a string that indicates the LB
policy name.  In JSON form, that string can be any arbitrary value, not
just one of the supported policies inside of the `oneof`, so third-party
policies can be selected.

# Architecture

A service config is associated with a server name.  The [name
resolver](naming.md) plugin, when asked to resolve a particular server
name, will return both the resolved addresses and the service config.

The name resolver returns the service config to the gRPC client in JSON form.
Individual resolver implementations determine where and in what format the
service config is stored.  If the resolver implemention obtains the
service config in protobuf form, it must convert it to JSON.
Alternatively, a resolver implementation may obtain the service config
already in JSON form, in which case it may return it directly.  Or it
may construct the JSON dynamically from some other source data.

For details of how the DNS resolver plugin supports service configs, see
[gRFC A2: Service Config via
DNS](https://github.com/grpc/proposal/blob/master/A2-service-configs-in-dns.md).

# Example

Here is an example service config in protobuf form:

```
{
  // Use round_robin LB policy.
  load_balancing_config: { round_robin: {} }
  // This method config applies to method "foo/bar" and to all methods
  // of service "baz".
  method_config: {
    name: {
      service: "foo"
      method: "bar"
    }
    name: {
      service: "baz"
    }
    // Default timeout for matching methods.
    timeout: {
      seconds: 1
      nanos: 1
    }
  }
}
```

Here is the same example service config in JSON form:

```
{
  "loadBalancingConfig": [ { "round_robin": {} } ],
  "methodConfig": [
    {
      "name": [
        { "service": "foo", "method": "bar" },
        { "service": "baz" }
      ],
      "timeout": "1.000000001s"
    }
  ]
}
```

# APIs

The service config is used in the following APIs:

- In the resolver API, used by resolver plugins to return the service
  config to the gRPC client.
- In the gRPC client API, where users can query the channel to obtain
  the service config associated with the channel (for debugging
  purposes).
- In the gRPC client API, where users can set the service config
  explicitly.  This can be used to set the config in unit tests.  It can
  also be used to set the default config that will be used if the
  resolver plugin does not return a service config.
