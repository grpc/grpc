# gRPC C++ CSM Hello World Example

This CSM example builds on the [Hello World Example](https://github.com/grpc/grpc/tree/master/examples/cpp/helloworld) and changes the gRPC client and server to accept configuration from an xDS control plane and test SSA and CSM observability

## Configuration

The client takes the following command-line arguments -
* target - By default, the client tries to connect to the xDS "xds:///helloworld:50051" and gRPC would use xDS to resolve this target and connect to the server backend. This can be overriden to change the target.

The server takes the following command-line arguments -
* port - Port on which the Hello World service is run. Defaults to 50051.
