#gRPC Naming and Discovery Support

## Overview

gRPC supports DNS as the default name-system. A number of alternative name-systems are used in various deployments. We propose an API that is general enough to support a range of name-systems and the corresponding syntax for names. The gRPC client library in various languages will provide a plugin mechanism so resolvers for different name-systems can be plugged in.

## Detailed Proposal

 A fully qualified, self contained name used for gRPC channel construction uses the syntax:

```
scheme://authority/endpoint_name
```

Here, scheme indicates the name-system to be used. Example schemes to be supported include: 

* `dns`

* `etcd`

Authority indicates some scheme-specific bootstrap information, e.g., for DNS, the authority may include the IP[:port] of the DNS server to use. Often, a DNS name may used as the authority, since the ability to resolve DNS names is already built into all gRPC client libraries.

Finally, the  endpoint_name indicates a concrete name to be looked up in a given name-system identified by the scheme and the authority. The syntax of endpoint name is dictated by the scheme in use.

### Plugins

The gRPC client library will switch on the scheme to pick the right resolver plugin and pass it the fully qualified name string.

Resolvers should be able to contact the authority and get a resolution that they return back to the gRPC client library. The returned contents include a list of IP:port, an optional config and optional auth config data to be used for channel authentication. The plugin API allows the resolvers to continuously watch an endpoint_name and return updated resolutions as needed. 

