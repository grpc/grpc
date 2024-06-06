# gRPC C++ CSM Hello World Example

This CSM example builds on the [Hello World Example](https://github.com/grpc/grpc/tree/master/examples/cpp/helloworld) and changes the gRPC client and server to test CSM observability.

## Configuration

The client takes the following command-line arguments -
* target - By default, the client tries to connect to the xDS "xds:///helloworld:50051" and gRPC would use xDS to resolve this target and connect to the server backend. This can be overridden to change the target.
* prometheus_endpoint - Endpoint used for prometheus. Default value is localhost:9464


The server takes the following command-line arguments -
* port - Port on which the Hello World service is run. Defaults to 50051.
* prometheus_endpoint - Endpoint used for prometheus. Default value is localhost:9464

## Building

From the gRPC workspace folder:

Client:
```
docker build -f examples/cpp/csm/observability/Dockerfile.client
```
Server:
```
docker build -f examples/cpp/csm/observability/Dockerfile.server
```

To push to a registry, add a tag to the image either by adding a `-t` flag to `docker build` command above or run:

```
docker image tag ${sha from build command above} ${tag}
```

And then push the tagged image using `docker push`