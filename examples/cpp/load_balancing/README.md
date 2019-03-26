# gRPC C++ Load Balancing Tutorial

### Prerequisite
Make sure you have run the [hello world example](../helloworld) or understood the basics of gRPC. We will not dive into the details that have been discussed in the hello world example.

### Get the tutorial source code

The example code for this and our other examples lives in the `examples` directory. Clone this repository to your local machine by running the following command:


```sh
$ git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc
```

Change your current directory to examples/cpp/load_balancing

```sh
$ cd examples/cpp/load_balancing/
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

Additionally, we can configure the load balancing policy. (To see what load balancing policies are available, check out [this folder](https://github.com/grpc/grpc/tree/master/src/core/ext/filters/client_channel/lb_policy).)

In the client, set the load balancing policy of the channel via the channel arg (to, for example, Round Robin).

```cpp
  ChannelArguments args;
  // Set the load balancing policy for the channel.
  args.SetLoadBalancingPolicyName("round_robin");
  GreeterClient greeter(grpc::CreateCustomChannel(
      "localhost:50051", grpc::InsecureChannelCredentials(), args));
```

For a working example, refer to [greeter_client.cc](greeter_client.cc) and [greeter_server.cc](greeter_server.cc).

Build and run the client and the server with the following commands.

```sh
make
./greeter_server
```

```sh
./greeter_client
```

(Note that the case in this example is trivial because there is only one server resolved from the name.)