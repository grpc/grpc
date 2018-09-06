# gRPC C++ Hello World Tutorial

### Install gRPC
Make sure you have installed gRPC on your system. Follow the
[BUILDING.md](../../../BUILDING.md) instructions.

### Get the tutorial source code

The example code for this and our other examples lives in the `examples`
directory. Clone this repository to your local machine by running the
following command:


```sh
$ git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc
```

Change your current directory to examples/cpp/helloworld

```sh
$ cd examples/cpp/helloworld/
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
[helloworld.proto](../../protos/helloworld.proto). The `Greeting`
service has one method, `hello`, that lets the server receive a single
`HelloRequest`
message from the remote client containing the user's name, then send back
a greeting in a single `HelloReply`. This is the simplest type of RPC you
can specify in gRPC - we'll look at some other types later in this document.

```protobuf
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
$ make helloworld.grpc.pb.cc helloworld.pb.cc
```
Which internally invokes the proto-compiler as:

```sh
$ protoc -I ../../protos/ --grpc_out=. --plugin=protoc-gen-grpc=grpc_cpp_plugin ../../protos/helloworld.proto
$ protoc -I ../../protos/ --cpp_out=. ../../protos/helloworld.proto
```

### Writing a client

- Create a channel. A channel is a logical connection to an endpoint. A gRPC
  channel can be created with the target address, credentials to use and
  arguments as follows

    ```cpp
    auto channel = CreateChannel("localhost:50051", InsecureChannelCredentials());
    ```

- Create a stub. A stub implements the rpc methods of a service and in the
  generated code, a method is provided to created a stub with a channel:

    ```cpp
    auto stub = helloworld::Greeter::NewStub(channel);
    ```

- Make a unary rpc, with `ClientContext` and request/response proto messages.

    ```cpp
    ClientContext context;
    HelloRequest request;
    request.set_name("hello");
    HelloReply reply;
    Status status = stub->SayHello(&context, request, &reply);
    ```

- Check returned status and response.

    ```cpp
    if (status.ok()) {
      // check reply.message()
    } else {
      // rpc failed.
    }
    ```

For a working example, refer to [greeter_client.cc](greeter_client.cc).

### Writing a server

- Implement the service interface

    ```cpp
    class GreeterServiceImpl final : public Greeter::Service {
      Status SayHello(ServerContext* context, const HelloRequest* request,
          HelloReply* reply) override {
        std::string prefix("Hello ");
        reply->set_message(prefix + request->name());
        return Status::OK;
      }
    };

    ```

- Build a server exporting the service

    ```cpp
    GreeterServiceImpl service;
    ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    ```

For a working example, refer to [greeter_server.cc](greeter_server.cc).

### Writing asynchronous client and server

gRPC uses `CompletionQueue` API for asynchronous operations. The basic work flow
is
- bind a `CompletionQueue` to a rpc call
- do something like a read or write, present with a unique `void*` tag
- call `CompletionQueue::Next` to wait for operations to complete. If a tag
  appears, it indicates that the corresponding operation is complete.

#### Async client

The channel and stub creation code is the same as the sync client.

- Initiate the rpc and create a handle for the rpc. Bind the rpc to a
  `CompletionQueue`.

    ```cpp
    CompletionQueue cq;
    auto rpc = stub->AsyncSayHello(&context, request, &cq);
    ```

- Ask for reply and final status, with a unique tag

    ```cpp
    Status status;
    rpc->Finish(&reply, &status, (void*)1);
    ```

- Wait for the completion queue to return the next tag. The reply and status are
  ready once the tag passed into the corresponding `Finish()` call is returned.

    ```cpp
    void* got_tag;
    bool ok = false;
    cq.Next(&got_tag, &ok);
    if (ok && got_tag == (void*)1) {
      // check reply and status
    }
    ```

For a working example, refer to [greeter_async_client.cc](greeter_async_client.cc).

#### Async server

The server implementation requests a rpc call with a tag and then wait for the
completion queue to return the tag. The basic flow is

- Build a server exporting the async service

    ```cpp
    helloworld::Greeter::AsyncService service;
    ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:50051", InsecureServerCredentials());
    builder.RegisterService(&service);
    auto cq = builder.AddCompletionQueue();
    auto server = builder.BuildAndStart();
    ```

- Request one rpc

    ```cpp
    ServerContext context;
    HelloRequest request;
    ServerAsyncResponseWriter<HelloReply> responder;
    service.RequestSayHello(&context, &request, &responder, &cq, &cq, (void*)1);
    ```

- Wait for the completion queue to return the tag. The context, request and
  responder are ready once the tag is retrieved.

    ```cpp
    HelloReply reply;
    Status status;
    void* got_tag;
    bool ok = false;
    cq.Next(&got_tag, &ok);
    if (ok && got_tag == (void*)1) {
      // set reply and status
      responder.Finish(reply, status, (void*)2);
    }
    ```

- Wait for the completion queue to return the tag. The rpc is finished when the
  tag is back.

    ```cpp
    void* got_tag;
    bool ok = false;
    cq.Next(&got_tag, &ok);
    if (ok && got_tag == (void*)2) {
      // clean up
    }
    ```

To handle multiple rpcs, the async server creates an object `CallData` to
maintain the state of each rpc and use the address of it as the unique tag. For
simplicity the server only uses one completion queue for all events, and runs a
main loop in `HandleRpcs` to query the queue.

For a working example, refer to [greeter_async_server.cc](greeter_async_server.cc).




