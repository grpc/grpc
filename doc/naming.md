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

Most gRPC implementations support the following URI schemes:

- `dns:[//authority/]host[:port]` -- DNS (default)
  - `host` is the host to resolve via DNS.
  - `port` is the port to return for each address.  If not specified,
    443 is used (but some implementations default to 80 for insecure
    channels).
  - `authority` indicates the DNS server to use, although this is only
    supported by some implementations.  (In C-core, the default DNS
    resolver does not support this, but the c-ares based resolver
    supports specifying this in the form "IP:port".)

- `unix:path`, `unix://absolute_path` -- Unix domain sockets (Unix systems only)
  - `path` indicates the location of the desired socket.
  - In the first form, the path may be relative or absolute; in the
    second form, the path must be absolute (i.e., there will actually be
    three slashes, two prior to the path and another to begin the
    absolute path).

- `unix-abstract:abstract_path` -- Unix domain socket in abstract namespace (Unix systems only)
  - `abstract_path` indicates a name in the abstract namespace.
  - The name has no connection with filesystem pathnames.
  - No permissions will apply to the socket - any process/user may access the socket.
  - The underlying implementation of Abstract sockets uses a null byte ('\0')
    as the first character; the implementation will prepend this null. Do not include
    the null in `abstract_path`.

- `vsock:cid:port` -- VSOCK (Linux systems only)
  - `cid` is 32-bit Context Identifier (CID). It indicates the source or
    destination, which is either a virtual machine or the host.
  - `port` is a 32-bit port number. It differentiates between multiple
    services running on a single machine.

The following schemes are supported by the gRPC C-core implementation,
but may not be supported in other languages:

- `ipv4:address[:port][,address[:port],...]` -- IPv4 addresses
  - Can specify multiple comma-delimited addresses of the form `address[:port]`:
    - `address` is the IPv4 address to use.
    - `port` is the port to use.  If not specified, 443 is used.

- `ipv6:address[:port][,address[:port],...]` -- IPv6 addresses
  - Can specify multiple comma-delimited addresses of the form `address[:port]`:
    - `address` is the IPv6 address to use. To use with a `port` the `address`
      must enclosed in literal square brackets (`[` and `]`).  Example:
      `ipv6:[2607:f8b0:400e:c00::ef]:443` or `ipv6:[::]:1234`
    - `port` is the port to use.  If not specified, 443 is used.

In the future, additional schemes such as `etcd` could be added.

### Resolver Plugins

The gRPC client library will use the specified scheme to pick the right
resolver plugin and pass it the fully qualified name string.

Resolvers should be able to contact the authority and get a resolution
that they return back to the gRPC client library. The returned contents
include:

- A list of resolved addresses (both IP address and port).  Each address
  may have a set of arbitrary attributes (key/value pairs) associated with
  it, which can be used to communicate information from the resolver to the
  [load balancing](load-balancing.md) policy.
- A [service config](service_config.md).

The plugin API allows the resolvers to continuously watch an endpoint
and return updated resolutions as needed.
