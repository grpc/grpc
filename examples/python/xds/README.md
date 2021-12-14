gRPC Hostname Example
=====================

The hostname example is a Hello World server whose response includes its
hostname. It also supports health and reflection services. This makes it a good
server to test infrastructure, like load balancing. This example depends on a
gRPC version of 1.28.1 or newer.

### Run the Server

1. Navigate to this directory:

```sh
cd grpc/examples/python/xds
```

2. Run the server

```sh
virtualenv venv -p python3
source venv/bin/activate
pip install -r requirements.txt
python server.py
```

### Run the Client

1. Set up xDS configuration.

After configuring your xDS server to track the gRPC server we just started,
create a bootstrap file as desribed in [gRFC A27](https://github.com/grpc/proposal/blob/master/A27-xds-global-load-balancing.md):

```
{
  xds_servers": [
    {
      "server_uri": <string containing URI of xds server>,
      "channel_creds": [
        {
          "type": <string containing channel cred type>,
          "config": <JSON object containing config for the type>
        }
      ]
    }
  ],
  "node": <JSON form of Node proto>
}
```

2. Point the `GRPC_XDS_BOOTSTRAP` environment variable at the bootstrap file:

```
export GRPC_XDS_BOOTSTRAP=/etc/xds-bootstrap.json
```

3. Run the client:

```
python client.py xds:///my-backend
```

### Verifying Configuration with a CLI Tool

Alternatively, `grpcurl` can be used to verify your server. If you don't have it,
install [`grpcurl`](https://github.com/fullstorydev/grpcurl/releases). This will allow
you to manually test the service.

Be sure to set up the bootstrap file and `GRPC_XDS_BOOTSTRAP` as in the previous
section.

1. Verify the server's application-layer service:

```sh
> grpcurl --plaintext -d '{"name": "you"}' localhost:50051
{
  "message": "Hello you from rbell.svl.corp.google.com!"
}
```

2. Verify that all services are available via reflection:

```sh
> grpcurl --plaintext localhost:50051 list
grpc.health.v1.Health
grpc.reflection.v1alpha.ServerReflection
helloworld.Greeter
```

3. Verify that all services are reporting healthy:

```sh
> grpcurl --plaintext -d '{"service": "helloworld.Greeter"}' localhost:50051
grpc.health.v1.Health/Check
{
  "status": "SERVING"
}

> grpcurl --plaintext -d '{"service": ""}' localhost:50051
grpc.health.v1.Health/Check
{
  "status": "SERVING"
}
```

### Running with Proxyless Security

#### Run the Server with Secure Credentials

Add the `--secure true` flag to the invocation outlined above.

```sh
python server.py --secure true
```

#### Run the Client with Secure Credentials

Add the `--secure true` flag to the invocation outlined above.

3. Run the client:

```
python client.py xds:///my-backend --secure true
```
