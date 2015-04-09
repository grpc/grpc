#gRPC in 3 minutes (C++)

## Installation

To install gRPC on your system, follow the instructions here:
[https://github.com/grpc/grpc/blob/master/INSTALL](https://github.com/grpc/grpc/blob/master/INSTALL).

## Hello C++ gRPC!

Here's how to build and run the C++ implementation of the [Hello World](https://github.com/grpc/grpc-common/blob/master/protos/helloworld.proto) example used in [Getting started](https://github.com/grpc/grpc-common).

The example code for this and our other examples lives in the `grpc-common`
GitHub repository. Clone this repository to your local machine by running the
following command:


```sh
$ git clone https://github.com/grpc/grpc-common.git
```

Change your current directory to grpc-common/cpp/helloworld

```sh
$ cd grpc-common/cpp/helloworld/
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

### Client and server implementations

The client implementation is at [greeter_client.cc](https://github.com/grpc/grpc-common/blob/master/cpp/helloworld/greeter_client.cc).

The server implementation is at [greeter_server.cc](https://github.com/grpc/grpc-common/blob/master/cpp/helloworld/greeter_server.cc).

### Try it!
Build client and server:
```sh
$ make
```
Run the server, which will listen on port 50051:
```sh
$ ./greeter_server
```
Run the client (in a different terminal):
```sh
$ ./greeter_client
```
If things go smoothly, you will see the "Greeter received: Hello world" in the client side output.

## Tutorial

You can find a more detailed tutorial in [gRPC Basics: C++](https://github.com/grpc/grpc-common/blob/master/cpp/cpptutorial.md)
