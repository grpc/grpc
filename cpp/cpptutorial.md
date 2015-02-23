#gRPC Basics: C++

This tutorial provides a basic C++ programmer's introduction to working with gRPC. By walking through this example you'll learn how to:

- Define a service in a .proto file.
- Generate server and client code using the protocol buffer compiler.
- Use the C++ gRPC API to write a simple client and server for your service.

It assumes that you have read the [Getting started](https://github.com/grpc/grpc-common) guide and are familiar with [protocol buffers] (https://developers.google.com/protocol-buffers/docs/overview). Note that the example in this tutorial uses the proto3 version of the protocol buffers language, which is currently in alpha release: you can see the [release notes](https://github.com/google/protobuf/releases) for the new version in the protocol buffers Github repository.

This isn't a comprehensive guide to using gRPC in C++: more reference documentation is coming soon.

## Why use gRPC?

Our example is a simple route mapping application that lets clients get information about features on their route, create a summary of their route, and exchange route information such as traffic updates with the server and other clients.

With gRPC we can define our service once in a .proto file and implement clients and servers in , which in turn - all the complexity of communication between different languages and environments is handled for you by gRPC. gRPC also 

We also get all the advantages of working with protocol buffers

[possibly insert more advantages here]

## Example code and setup

The example code for our tutorial is in [grpc/grpc-common/cpp/route_guide](https://github.com/grpc/grpc-common/cpp/route_guide). To download the example, clone the `grpc-common` repository by running the following command:
```shell
$ git clone https://github.com/google/grpc-common.git
```

Then change your current directory to `grpc-common/cpp/route_guide`:
```shell
$ cd grpc-common/cpp/route_guide
```

Although we've provided the complete example so you don't need to generate the gRPC code yourself, if you want to try generating your own server and client interface code you can follow the setup instructions for the C++ gRPC libraries in [grpc/grpc/INSTALL](https://github.com/grpc/grpc/blob/master/INSTALL).


## Defining the service

Our first step (as you'll know from [Getting started](https://github.com/grpc/grpc-common)) is to define the gRPC *service* and the method *request* and *response* types using [protocol buffers] (https://developers.google.com/protocol-buffers/docs/overview). You can see the complete .proto file in [`grpc-common/protos/route_guide.proto`](https://github.com/grpc/grpc-common/blob/master/protos/route_guide.proto).

To define a service, you specify a named `service` in your .proto file:

```
service RouteGuide {
   ...
}
```

Then you define `rpc` methods inside your service definition, specifying their request and response types. gRPC lets you define four kinds of service method, all of which are used in the `RouteGuide` service:

- A *simple RPC* where the client sends a request to the server using the stub and waits for a response to come back, just like a normal function call.
```
 // Obtains the feature at a given position.
   rpc GetFeature(Point) returns (Feature) {}
```

- A *server-side streaming RPC* where the client sends a request to the server and gets a stream to read a sequence of messages back. The client reads from the returned stream until there are no more messages.
```
  // Obtains the Features available within the given Rectangle.  Results are
  // streamed rather than returned at once (e.g. in a response message with a
  // repeated field), as the rectangle may cover a large area and contain a
  // huge number of features.
  rpc ListFeatures(Rectangle) returns (stream Feature) {}
```

- A *client-side streaming RPC* where the client writes a sequence of messages and sends them to the server, again using a provided stream. Once the client has finished writing the messages, it waits for the server to read them all and return its response.
```
  // Accepts a stream of Points on a route being traversed, returning a
  // RouteSummary when traversal is completed.
  rpc RecordRoute(stream Point) returns (RouteSummary) {}
```

- A *bidirectional streaming RPC* where both sides send a sequence of messages using a read-write stream. The two streams operate independently, so clients and servers can read and write in whatever order they like: for example, the server could wait to receive all the client messages before writing its responses, or it could alternately read a message then write a message, or some other combination of reads and writes. The order of messages in each stream is preserved.
```
  // Accepts a stream of Points on a route being traversed, returning a
  // RouteSummary when traversal is completed.
  rpc RecordRoute(stream Point) returns (RouteSummary) {}
```

Our .proto file also contains protocol buffer message type definitions for all the request and response types used in our service methods - for example, here's the `Point` message type:
```
  // Accepts a stream of Points on a route being traversed, returning a
  // RouteSummary when traversal is completed.
  rpc RecordRoute(stream Point) returns (RouteSummary) {}
```


## Generating client and server code

Now we need to ...


## Creating the server

First let's look at implementing our R

There are two parts to making our `RouteGuide` service work:
- 


## Creating the client






