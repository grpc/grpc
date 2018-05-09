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
uses URI syntax as defined in [RFC 3986](https://tools.ietf.org/html/rfc3986).

The URI scheme indicates what resolver plugin to use.  If no scheme
prefix is specified or the scheme is unknown, the `dns` scheme is used
by default.

The URI path indicates the name to be resolved.

We currently support the following URI schemes:

- `dns:[//authority/]host[:port]` -- DNS (default)
  - `host` is the host to resolve via DNS.
  - `port` is the port to return for each address.  If not specified,
    443 is used.
  - `authority` is not supported with the default DNS resolver.  With the
    c-ares based DNS resolver, the `authority` can be used to indicate
    which DNS server to query, which can be specified in the form "IP:port".

- `unix:path` or `unix://absolute_path` -- Unix domain sockets
  - `path` indicates the location of the desired socket.
  - In the first form, the path may be relative or absolute; in the
    second form, the path must be absolute.

In the future, additional schemes such as `etcd` could be added.

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
