# gRPC C++ xDS Hello World Example

This xDS example builds on the [Hello World Example](https://github.com/grpc/grpc/tree/master/examples/cpp/helloworld) and changes the gRPC client and server to accept configuration from an xDS control plane.

## Configuration

The client takes two command-line arguments -
* target - By default, the client tries to connect to the xDS "xds:///helloworld:50051" and gRPC would use xDS to resolve this target and connect to the server backend. This can be overriden to change the target.
* secure - Bool value, defaults to true. When this is set, [XdsCredentials](https://github.com/grpc/proposal/blob/master/A29-xds-tls-security.md) will be used with a fallback on `InsecureChannelCredentials`. If unset, `InsecureChannelCredentials` will be used.

The server takes three command-line arguments -
* port - Port on which the Hello World service is run. Defaults to 50051.
* mantenance_port - If secure mode is used (see below), the [Admin](https://github.com/grpc/proposal/blob/master/A38-admin-interface-api.md) service is exposed on this port. If secure mode is not used, `maintenance_port` is unused, and the Admin service is just exposed on `port`. Defaults to 50052.
* secure - Bool value, defaults to true. When this is set, [XdsServerCredentials](https://github.com/grpc/proposal/blob/master/A29-xds-tls-security.md) will be used with a fallback on `InsecureServerCredentials`. If unset, `InsecureServerCredentials` will be used.

## Running the example

Currently, this example and some of the gRPC xDS libraries that it depends on only builds with bazel. CMake support will be introduced in the future.

To use XDS, you should first deploy the XDS management server in your deployment environment and know its name. You need to set the `GRPC_XDS_BOOTSTRAP` environment variable to point to the gRPC XDS bootstrap file (see [gRFC A27](https://github.com/grpc/proposal/blob/master/A27-xds-global-load-balancing.md#xdsclient-and-bootstrap-file) for the bootstrap format). This is needed by both client and server.

Please view [GCP instructions](https://cloud.google.com/traffic-director/docs/security-proxyless-setup) as an example.

To run the server -

```
$ export GRPC_XDS_BOOTSTRAP=/path/to/bootstrap.json
$ tools/bazel run examples/cpp/xds:greeter_server
```

To run the client -

```
$ export GRPC_XDS_BOOTSTRAP=/path/to/bootstrap.json
$ tools/bazel run examples/cpp/xds:greeter_client
```

