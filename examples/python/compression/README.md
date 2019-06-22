## Compression with gRPC Python

gRPC offers lossless compression options in order to decrease the number of bits
transferred over the wire. Three levels of compression are available:

 - `grpc.Compression.NoCompression` - No compression is applied to the payload. (default)
 - `grpc.Compression.Deflate` - The "Deflate" algorithm is applied to the payload.
 - `grpc.Compression.Gzip` - The Gzip algorithm is applied to the payload.

The default option on both clients and servers is `grpc.Compression.NoCompression`.

See [the gRPC Compression Spec](https://github.com/grpc/grpc/blob/master/doc/compression.md)
for more information.

### Client Side Compression

Compression may be set at two levels on the client side.

#### At the channel level

```python
with grpc.insecure_channel('foo.bar:1234', compression=grpc.Compression.Gzip) as channel:
    use_channel(channel)
```

#### At the call level

Setting the compression method at the call level will override any settings on
the channel level.

```python
stub = helloworld_pb2_grpc.GreeterStub(channel)
response = stub.SayHello(helloworld_pb2.HelloRequest(name='you'),
                         compression=grpc.Compression.Deflate)
```


### Server Side Compression

Additionally, compression may be set at two levels on the server side.

#### On the entire server

```python
server = grpc.server(futures.ThreadPoolExecutor(),
                     compression=grpc.Compression.Gzip)
```

#### For an individual RPC

```python
def SayHello(self, request, context):
    context.set_response_compression(grpc.Compression.NoCompression)
    return helloworld_pb2.HelloReply(message='Hello, %s!' % request.name)
```

Setting the compression method for an individual RPC will override any setting
supplied at server creation time.
