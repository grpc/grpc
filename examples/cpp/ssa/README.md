# gRPC C++ xDS Server-Side Affinity (SSA) Example

This xDS example builds on the [xDS Example](https://github.com/grpc/grpc/tree/master/examples/cpp/xds)
and demonstrates how a gRPC client can utilize Server-Side Affinity (SSA)
by exchanging session cookies with the server, guided by xDS configuration.

## Introduction to Server-Side Affinity (SSA)

Server-Side Affinity (SSA) ensures that subsequent requests from a client
are routed to the same backend server that handled an initial request, as long
as that backend is available. In gRPC, this is often achieved through
a session cookie exchanged between the client and the server, with the gRPC
configured to understand and utilize this cookie for routing decisions.

This example specifically shows:

  * Making an initial request without a session cookie.
  * Extracting cookie value from the `set-cookie` header the gRPC implementation
    set.
  * Includes this session cookie in subsequent requests using the `cookie`
    header, allowing the gRPC load balancer to route these requests to the same
    backend.

**Note:** This client example demonstrates the mechanism of sending and
receiving cookies for SSA. A production-ready client would typically also
implement logic to respect the `Max-Age` attribute of the cookie and
invalidate/remove the cookie when it expires to ensure correct routing behavior.
This example *does not* include the full cookie expiration logic.

## Configuration

The client takes three command-line arguments -

  * `target` - By default, the client tries to connect to the xDS
    "xds:///helloworld:50051". gRPC would use xDS to resolve this target and
    connect to the server backend. This can be overridden to change the target.
  * `secure` - Bool value, defaults to `true`. When this is set,
    [XdsCredentials](https://github.com/grpc/proposal/blob/master/A29-xds-tls-security.md)
    will be used with a fallback on `InsecureChannelCredentials`. If unset,
    `InsecureChannelCredentials` will be used.
  * `ssa_cookie` - The name of the gRPC session cookie. Defaults to
    "grpc_session_cookie". This *must* match the cookie name configured in your
    xDS control plane for SSA.

## Running the example

To use XDS, you should first deploy the XDS management server in your deployment
environment and know its name. You need to set the `GRPC_XDS_BOOTSTRAP`
environment variable to point to the gRPC XDS bootstrap file
(see [gRFC A27](https://github.com/grpc/proposal/blob/master/A27-xds-global-load-balancing.md#xdsclient-and-bootstrap-file) for the bootstrap format). This is needed by both client and server.

Please view [GCP instructions](https://cloud.google.com/traffic-director/docs/security-proxyless-setup)
as an example of setting up Traffic Director with xDS. For SSA, ensure your
xDS configuration (e.g., in Traffic Director, via a forwarding rule or URL map)
is set up to issue and interpret the `grpc_session_cookie` for backend routing.