# Default HTTP Proxy Mapper User Guide for gRPC Core (and dependents)

[A1-http-connect-proxy-support.md](https://github.com/grpc/proposal/blob/master/A1-http-connect-proxy-support.md)
proposed how gRPC supports TCP-level proxies via the HTTP CONNECT request,
defined in [RFC-2817](https://www.rfc-editor.org/rfc/rfc2817).

**Case 1** in the proposal documents a use-case where all outbound traffic from
an environment must go through a proxy. Configurations for such environments are
usually performed using environment variables such as `http_proxy`. gRPC
supports this by providing a default proxy mapper implementation that allows for
overriding the server name (provided in the channel creation hostname) to
resolve based on such configurations.

This guide documents gRPC C-Core's default proxy mapper implementation.

## Working

### Enabling HTTP Proxy

C-Core checks the following places to determine the HTTP proxy to use, stopping
at the first one that is set:

1.  `GRPC_ARG_HTTP_PROXY` channel arg
2.  `grpc_proxy` environment variable
3.  `https_proxy` environment variable
4.  `http_proxy` environment variable

If none of the above are set, then no HTTP proxy will be used.

The allowed format is an [RFC3986](https://www.rfc-editor.org/rfc/rfc3986) URI
string where the scheme is expected to be "http" and the authority portion is
used to determine the proxy to be used. For example, for an HTTP proxy setting
of `http://username:password@proxy.google.com:443`, `username:password` would be
used as user credentials for proxy authentication as per
[RFC7617](https://www.rfc-editor.org/rfc/rfc7617) and `proxy.google.com:443`
would be the host:port HTTP proxy target. If the port part of the authority is
omitted, a default port of 443 is used. Note that user credential can also be
omitted if the proxy does not need authentication.

### Disabling HTTP Proxy

If an HTTP proxy is set, C-Core then checks the following places to exclude
traffic destined to listed hosts from going through the proxy determined above,
again stopping at the first one that is set:

1.  `no_grpc_proxy` environment variable
2.  `no_proxy`environment variable

If none of the above are set, then the previously found HTTP proxy is used.

The format takes a comma-separated list of names, and if any of these names
matches as a suffix of the server host (provided in the channel target), then
the proxy will not be used for that target. For example, with a `grpc_proxy`
setting of `proxy.google.com` and a `no_grpc_proxy` setting of `example.com,
google.com`, channel targets such as `dns:///foo.google.com:50051` and
`bar.example.com:1234` will not use the proxy, but `baz.googleapis.com:443`
would still use the configured proxy `proxy.google.com`.

As of [PR#31119](https://github.com/grpc/grpc/pull/31119), CIDR blocks are also
supported in the list of names. For example, a `no_proxy` setting of
`10.10.0.0/24` would not use the proxy for channel targets that mention IP
addresses as the host between the range `10.10.0.0` to `10.10.0.255`.

### Disabling HTTP Proxy Channel-wide

The lookup and subsequent usage of an HTTP proxy for a specific channel can also
be disabled by setting the channel arg `GRPC_ARG_ENABLE_HTTP_PROXY` to 0.
