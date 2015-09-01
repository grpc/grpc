# gRPC Python Hello World

This is a quick introduction with a simple example and installation instructions: for a more complete tutorial see [gRPC Basics: Python](../route_guide).

### Install gRPC
Make sure you have built gRPC Python from source on your system. Follow the instructions here:
[https://github.com/grpc/grpc/blob/master/src/python/README.md](https://github.com/grpc/grpc/blob/master/src/python/README.md).

This gives you a python virtual environment with installed gRPC Python
in GRPC_ROOT/python2.7_virtual_environment. GRPC_ROOT is the path to which you
have cloned the [gRPC git repo](https://github.com/grpc/grpc).

### Get the source code

The example code for our Hello World and our other examples live in the `examples`
directory. Clone this repository to your local machine by running the
following command:


```sh
$ git clone https://github.com/grpc/grpc.git
```

Change your current directory to examples/python/helloworld

```sh
$ cd examples/python/helloworld/
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

Here's our example service definition. The `Greeting`
service has one method, `hello`, that lets the server receive a single
`HelloRequest`
message from the remote client containing the user's name, then send back
a greeting in a single `HelloReply`. This is the simplest type of RPC you
can specify in gRPC.

```
syntax = "proto3";

option java_package = "io.grpc.examples";

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
$ ./run_codegen.sh
```
Which internally invokes the proto-compiler as:

```sh
$ protoc -I ../../protos --python_out=. --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_python_plugin` ../../protos/helloworld.proto
```

Optionally, you can just skip the code generation step as the generated python module has already
been generated for you (helloworld_pb2.py).

### The client

Client-side code can be found in [greeter_client.py](greeter_client.py).

You can run the client using:

```sh
$ ./run_client.sh
```


### The server

Server side code can be found in [greeter_server.py](greeter_server.py). 

You can run the server using:

```sh
$ ./run_server.sh
```
