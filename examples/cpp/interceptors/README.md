# gRPC C++ Interceptors Example

The C++ Interceptors example shows how interceptors might be used with a simple key-value store.

## Key Value Store

The key-value store service is defined in [keyvaluestore.proto](https://github.com/grpc/grpc/blob/master/examples/protos/keyvaluestore.proto).It has a simple bidi streaming RPC where the request messages contain a key and the response messages contain a value.

The example shows a very naive CachingInterceptor added on the client channel that caches the key-value pairs that it sees. If the client looks up a key present in the cache, the interceptor responds with its saved value and the server doesn't see the request for that key.

On the server-side, a very simple logging interceptor is added that simply logs to stdout whenever a new RPC is received.

## Running the example

To run the server -

```
$ tools/bazel run examples/cpp/interceptors:server
```

To run the client (on a different terminal) -

```
$ tools/bazel run examples/cpp/interceptors:client
```
