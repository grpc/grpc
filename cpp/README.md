#gRPC C++ Getting started

First you need to install gRPC on your system. Follow the instructions here:
[https://github.com/grpc/grpc/blob/master/INSTALL](https://github.com/grpc/grpc/blob/master/INSTALL).

# gRPC C++ Hello World Tutorial

### Install gRPC
Make sure you have installed gRPC on your system. Follow the instructions here:
[https://github.com/grpc/grpc/blob/master/INSTALL](https://github.com/grpc/grpc/blob/master/INSTALL).

### Get the tutorial source code

The example code for this and our other examples lives in the `grpc-common`
GitHub repository. Clone this repository to your local machine by running the
following command:


```sh
$ git clone https://github.com/google/grpc-common.git
```

Change your current directory to grpc-common/cpp/helloworld

```sh
$ cd grpc-common/cpp/helloworld/
```

### Defining a service

The first step in creating our example is to define a *service*: an RPC
service specifies the methods that can be called remotely with their parameters
and return types. As you saw in the
[overview](#protocolbuffers) above, gRPC does this using [protocol
buffers](https://developers.google.com/protocol-buffers/docs/overview). We
use the protocol buffers interface definition language (IDL) to define our
service methods, and define the parameters and return
types as protocol buffer message types. Both the client and the
server use interface code generated from the service definition.

Here's our example service definition, defined using protocol buffers IDL in
[helloworld.proto](https://github.com/grpc/grpc-common/blob/master/protos/helloworld.proto). The `Greeting`
service has one method, `hello`, that lets the server receive a single
`HelloRequest`
message from the remote client containing the user's name, then send back
a greeting in a single `HelloReply`. This is the simplest type of RPC you
can specify in gRPC - we'll look at some other types later in this document.

```
syntax = "proto3";

option java_package = "ex.grpc";

package helloworld;

// The greeting service definition.
service Greeter {
  // Sends a greeting
  rpc SayHello (HelloRequest) returns (HelloReply) {}
}

// The request message containing the user's name.
message HelloRequest {
  string name = 1;
}

// The response message containing the greetings
message HelloReply {
  string message = 1;
}

```

<a name="generating"></a>
### Generating gRPC code

Once we've defined our service, we use the protocol buffer compiler
`protoc` to generate the special client and server code we need to create
our application. The generated code contains both stub code for clients to
use and an abstract interface for servers to implement, both with the method
defined in our `Greeting` service.

To generate the client and server side interfaces:

```sh
$ make helloworld.pb.cc
```
Which internally invokes the proto-compiler as:

```sh
$protoc -I ../../protos/ --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=grpc_cpp_plugin helloworld.proto
```

### Writing a client

This is an incomplete tutorial. For now the reader should refer to [greeter_client.cc](https://github.com/grpc/grpc-common/blob/master/cpp/helloworld/greeter_client.cc).

### Writing a server

This is an incomplete tutorial. For now the reader should refer to [greeter_server.cc](https://github.com/grpc/grpc-common/blob/master/cpp/helloworld/greeter_server.cc).

A more detailed tutorial is coming soon.
