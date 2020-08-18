gRPC Hostname example (C#)
========================

BACKGROUND
-------------
This is a version of the helloworld example with a server whose response includes its hostname. It also supports health and reflection services. This makes it a good server to test infrastructure, such as XDS load balancing.

PREREQUISITES
-------------

- The [.NET Core SDK 2.1+](https://www.microsoft.com/net/core)

You can also build the solution `Greeter.sln` using Visual Studio 2019,
but it's not a requirement.

RUN THE EXAMPLE
-------------

First, build and run the server, then verify the server is running and
check the server is behaving as expected (more on that below).

```
cd GreeterServer
dotnet run
```

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
cd GreeterClient
dotnet run --server xds-experimental:///my-backend
```

VERIFYING THE SERVER
-------------

`grpcurl` can be used to test your server. If you don't have it,
install [`grpcurl`](https://github.com/fullstorydev/grpcurl/releases). This will allow
you to manually test the service.

Exercise your server's application-layer service:

```sh
> grpcurl --plaintext -d '{"name": "you"}' localhost:30051
{
  "message": "Hello you from jtatt.muc.corp.google.com!"
}
```

Make sure that all of your server's services are available via reflection:

```sh
> grpcurl --plaintext localhost:30051 list
grpc.health.v1.Health
grpc.reflection.v1alpha.ServerReflection
helloworld.Greeter
```

Make sure that your services are reporting healthy:

```sh
> grpcurl --plaintext -d '{"service": "helloworld.Greeter"}' localhost:30051
grpc.health.v1.Health/Check
{
  "status": "SERVING"
}

> grpcurl --plaintext -d '{"service": ""}' localhost:30051
grpc.health.v1.Health/Check
{
  "status": "SERVING"
}
```
