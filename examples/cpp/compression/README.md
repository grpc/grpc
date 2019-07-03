# gRPC C++ Message Compression Tutorial

### Prerequisite
Make sure you have run the [hello world example](../helloworld) or understood the basics of gRPC. We will not dive into the details that have been discussed in the hello world example.

### Get the tutorial source code

The example code for this and our other examples lives in the `examples` directory. Clone this repository to your local machine by running the following command:


```sh
$ git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc
```

Change your current directory to examples/cpp/compression

```sh
$ cd examples/cpp/compression/
```

### Generating gRPC code

To generate the client and server side interfaces:

```sh
$ make helloworld.grpc.pb.cc helloworld.pb.cc
```
Which internally invokes the proto-compiler as:

```sh
$ protoc -I ../../protos/ --grpc_out=. --plugin=protoc-gen-grpc=grpc_cpp_plugin ../../protos/helloworld.proto
$ protoc -I ../../protos/ --cpp_out=. ../../protos/helloworld.proto
```

### Writing a client and a server

The client and the server can be based on the hello world example.

Additionally, we can configure the compression settings.

In the client, set the default compression algorithm of the channel via the channel arg.

```cpp
  ChannelArguments args;
  // Set the default compression algorithm for the channel.
  args.SetCompressionAlgorithm(GRPC_COMPRESS_GZIP);
  GreeterClient greeter(grpc::CreateCustomChannel(
      "localhost:50051", grpc::InsecureChannelCredentials(), args));
```

Each call's compression configuration can be overwritten by client context.

```cpp
    // Overwrite the call's compression algorithm to DEFLATE.
    context.set_compression_algorithm(GRPC_COMPRESS_DEFLATE);
```

In the server, set the default compression algorithm via the server builder.

```cpp
  ServerBuilder builder;
  // Set the default compression algorithm for the server.
  builder.SetDefaultCompressionAlgorithm(GRPC_COMPRESS_GZIP);
```

Each call's compression configuration can be overwritten by server context.

```cpp
    // Overwrite the call's compression algorithm to DEFLATE.
    context->set_compression_algorithm(GRPC_COMPRESS_DEFLATE);
```

For a working example, refer to [greeter_client.cc](greeter_client.cc) and [greeter_server.cc](greeter_server.cc).

Build and run the (compressing) client and the server by the following commands.

```sh
make
./greeter_server
```

```sh
./greeter_client
```
