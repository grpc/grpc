gRPC Hostname Example
=====================

The hostname example is a Hello World server whose response includes its
hostname. It also supports health and reflection services. This makes it a good
server to test infrastructure, like load balancing .This example depends on a
gRPC version of 1.28.1 or newer.

### Run the example

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

3. Verify the Server

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

Then point the `GRPC_XDS_BOOTSTRAP` environment variable at the bootstrap file:

```
export GRPC_XDS_BOOTSTRAP=/etc/xds-bootstrap.json
```

Finally, run your client:

```
python client.py xds:///my-backend
```

Alternatively, `grpcurl` can be used to test your server. If you don't have it,
install [`grpcurl`](https://github.com/fullstorydev/grpcurl/releases). This will allow
you to manually test the service.

Exercise your server's application-layer service:

```sh
> grpcurl --plaintext -d '{"name": "you"}' localhost:50051
{
  "message": "Hello you from rbell.svl.corp.google.com!"
}
```

Make sure that all of your server's services are available via reflection:

```sh
> grpcurl --plaintext localhost:50051 list
grpc.health.v1.Health
grpc.reflection.v1alpha.ServerReflection
helloworld.Greeter
```

Make sure that your services are reporting healthy:

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
