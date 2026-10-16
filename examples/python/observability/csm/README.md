# gRPC Python CSM Hello World Example

This CSM example builds on the [Python xDS Example](https://github.com/grpc/grpc/tree/master/examples/python/xds) and changes the gRPC client and server to accept configuration from an xDS control plane and test CSM observability.

## Configuration

The client takes the following command-line arguments -
* `--target` - By default, the client tries to connect to the target "xds:///helloworld:50051" and gRPC would use xDS to resolve this target and connect to the server backend. This can be overridden to change the target.
* `--secure_mode` - Whether to use xDS to retrieve server credentials. Default value is False.
* `--prometheus_endpoint` - Endpoint used for prometheus. Default value is localhost:9464.


The server takes the following command-line arguments -
* `--port` - Port on which the Hello World service is run. Defaults to 50051.
* `--secure_mode` - Whether to use xDS to retrieve server credentials. Default value is False.
* `--server_id` - The server ID to return in responses.
* `--prometheus_endpoint` - Endpoint used for prometheus. Default value is `localhost:9464`.

## Building

From the gRPC workspace folder:

Client:
```
docker build -f examples/python/observability/csm/Dockerfile.client -t "us-docker.pkg.dev/grpc-testing/examples/csm-o11y-example-python-client" .
```

Server:
```
docker build -f examples/python/observability/csm/Dockerfile.server -t "us-docker.pkg.dev/grpc-testing/examples/csm-o11y-example-python-server" .
```

And then push the tagged image using `docker push`.
