# gRPC Name Resolution

## Overview

gRPC supports DNS as the default name-system. A number of alternative
name-systems are used in various deployments. We support an API that is
general enough to support a range of name-systems and the corresponding
syntax for names. The gRPC client library in various languages will
provide a plugin mechanism so resolvers for different name-systems can
be plugged in.

## Detailed Design

### Name Syntax

A fully qualified, self contained name used for gRPC channel construction
uses the syntax:

```
scheme://authority/endpoint_name
```

Here, `scheme` indicates the name-system to be used. Currently, we
support the following schemes:

- `dns`

- `ipv4` (IPv4 address)

- `ipv6` (IPv6 address)

- `unix` (path to unix domain socket -- unix systems only)

In the future, additional schemes such as `etcd` could be added.

The `authority` indicates some scheme-specific bootstrap information, e.g.,
for DNS, the authority may include the IP[:port] of the DNS server to
use. Often, a DNS name may be used as the authority, since the ability to
resolve DNS names is already built into all gRPC client libraries.

Finally, the `endpoint_name` indicates a concrete name to be looked up
in a given name-system identified by the scheme and the authority. The
syntax of the endpoint name is dictated by the scheme in use.

### Resolver Plugins

The gRPC client library will use the specified scheme to pick the right
resolver plugin and pass it the fully qualified name string.

Resolvers should be able to contact the authority and get a resolution
that they return back to the gRPC client library. The returned contents
include:

- A list of resolved addresses, each of which has three attributes:
  - The address itself, including both IP address and port.
  - A boolean indicating whether the address is a backend address (i.e.,
    the address to use to contact the server directly) or a balancer
    address (for cases where [external load balancing](load-balancing.md)
    is in use).
  - The name of the balancer, if the address is a balancer address.
    This will be used to perform peer authorization.
- A [service config](service_config.md).

The plugin API allows the resolvers to continuously watch an endpoint
and return updated resolutions as needed.
