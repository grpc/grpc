#gRPC Basics: Objective-C

This tutorial provides a basic Objective-C programmer's introduction to working with gRPC. By walking through this example you'll learn how to:

- Define a service in a .proto file.
- Generate client code using the protocol buffer compiler.
- Use the Objective-C gRPC API to write a simple client for your service.

It assumes a passing familiarity with [protocol buffers](https://developers.google.com/protocol-buffers/docs/overview). Note that the example in this tutorial uses the proto3 version of the protocol buffers language, which is currently in alpha release: you can find out more in the [proto3 language guide](https://developers.google.com/protocol-buffers/docs/proto3) and see the [release notes](https://github.com/google/protobuf/releases) for the new version in the protocol buffers Github repository.

This isn't a comprehensive guide to using gRPC in Objective-C: more reference documentation is coming soon.

- [Why use gRPC?](#why-grpc)
- [Example code and setup](#setup)
- [Try it out!](#try)
- [Defining the service](#proto)
- [Generating client code](#protoc)
- [Creating the client](#client)

<a name="why-grpc"></a>
## Why use gRPC?

With gRPC you can define your service once in a .proto file and implement clients and servers in any of gRPC's supported languages, which in turn can be run in environments ranging from servers inside Google to your own tablet - all the complexity of communication between different languages and environments is handled for you by gRPC. You also get all the advantages of working with protocol buffers, including efficient serialization, a simple IDL, and easy interface updating.

gRPC and proto3 are specially suited for mobile clients: gRPC is implemented on top of HTTP/2, which results in network bandwidth savings over using HTTP/1.1. Serialization and parsing of the proto binary format is more efficient than the equivalent JSON, resulting in CPU and battery savings. And proto3 uses a runtime that has been optimized over the years at Google to keep code size to a minimum. The latter is important in Objective-C, because the ability of the compiler to strip unused code is limited by the dynamic nature of the language.


<a name="setup"></a>
## Example code and setup

The example code for our tutorial is in [grpc/grpc-common/objective-c/route_guide](https://github.com/grpc/grpc-common/tree/master/objective-c/route_guide). To download the example, clone the `grpc-common` repository by running the following command:
```shell
$ git clone https://github.com/grpc/grpc-common.git
```

Then change your current directory to `grpc-common/objective-c/route_guide`:
```shell
$ cd grpc-common/objective-c/route_guide
```

Our example is a simple route mapping application that lets clients get information about features on their route, create a summary of their route, and exchange route information such as traffic updates with the server and other clients.

You also should have [Cocoapods](https://cocoapods.org/#install) installed, as well as the relevant tools to generate the client library code (and a server in another language, for testing). You can obtain the latter by following [these setup instructions](https://github.com/grpc/homebrew-grpc).


<a name="try"></a>
## Try it out!

To try the sample app, we need a gRPC server running locally. Let's compile and run, for example, the C++ server in this repository:

```shell
$ pushd ../../cpp/route_guide
$ make
$ ./route_guide_server &
$ popd
```

Now have Cocoapods generate and install the client library for our .proto files:

```shell
$ pod install
```

(This might have to compile OpenSSL, which takes around 15 minutes if Cocoapods doesn't have it yet on your computer's cache).

Finally, open the XCode workspace created by Cocoapods, and run the app. You can check the calling code in `ViewControllers.m` and see the results in XCode's log console.

The next sections guide you step-by-step through how this proto service is defined, how to generate a client library from it, and how to create an app that uses that library.


<a name="proto"></a>
## Defining the service

First let's look at how the service we're using is defined. A gRPC *service* and its method *request* and *response* types using [protocol buffers](https://developers.google.com/protocol-buffers/docs/overview). You can see the complete .proto file for our example in [`grpc-common/protos/route_guide.proto`](https://github.com/grpc/grpc-common/blob/master/protos/route_guide.proto).

To define a service, you specify a named `service` in your .proto file:

```protobuf
service RouteGuide {
   ...
}
```

Then you define `rpc` methods inside your service definition, specifying their request and response types. Protocol buffers let you define four kinds of service method, all of which are used in the `RouteGuide` service:

- A *simple RPC* where the client sends a request to the server and receives a response later, just like a normal remote procedure call.
```protobuf
   // Obtains the feature at a given position.
   rpc GetFeature(Point) returns (Feature) {}
```

- A *response-streaming RPC* where the client sends a request to the server and gets back a stream of response messages. You specify a response-streaming method by placing the `stream` keyword before the *response* type.
```protobuf
  // Obtains the Features available within the given Rectangle.  Results are
  // streamed rather than returned at once (e.g. in a response message with a
  // repeated field), as the rectangle may cover a large area and contain a
  // huge number of features.
  rpc ListFeatures(Rectangle) returns (stream Feature) {}
```

- A *request-streaming RPC* where the client sends a sequence of messages to the server. Once the client has finished writing the messages, it waits for the server to read them all and return its response. You specify a request-streaming method by placing the `stream` keyword before the *request* type.
```protobuf
  // Accepts a stream of Points on a route being traversed, returning a
  // RouteSummary when traversal is completed.
  rpc RecordRoute(stream Point) returns (RouteSummary) {}
```

- A *bidirectional streaming RPC* where both sides send a sequence of messages to the other. The two streams operate independently, so clients and servers can read and write in whatever order they like: for example, the server could wait to receive all the client messages before writing its responses, or it could alternately read a message then write a message, or some other combination of reads and writes. The order of messages in each stream is preserved. You specify this type of method by placing the `stream` keyword before both the request and the response.
```protobuf
  // Accepts a stream of RouteNotes sent while a route is being traversed,
  // while receiving other RouteNotes (e.g. from other users).
  rpc RouteChat(stream RouteNote) returns (stream RouteNote) {}
```

Our .proto file also contains protocol buffer message type definitions for all the request and response types used in our service methods - for example, here's the `Point` message type:
```protobuf
// Points are represented as latitude-longitude pairs in the E7 representation
// (degrees multiplied by 10**7 and rounded to the nearest integer).
// Latitudes should be in the range +/- 90 degrees and longitude should be in
// the range +/- 180 degrees (inclusive).
message Point {
  int32 latitude = 1;
  int32 longitude = 2;
}
```

You can specify a prefix to be used for your generated classes by adding the `objc_class_prefix` option at the top of the file. For example:
```protobuf
option objc_class_prefix = "RTG";
```


<a name="protoc"></a>
## Generating client code

Next we need to generate the gRPC client interfaces from our .proto service definition. We do this using the protocol buffer compiler (`protoc`) with a special gRPC Objective-C plugin.

For simplicity, we've provided a [Podspec file](https://github.com/grpc/grpc-common/blob/master/objective-c/route_guide/RouteGuide.podspec) that runs `protoc` for you with the appropriate plugin, input, and output, and describes how to compile the generated files. You just need to run in this directory (`grpc-common/objective-c/route_guide`):

```shell
$ pod install
```

which, before installing the generated library in the XCode project of this sample, runs:

```shell
$ protoc -I ../../protos --objc_out=Pods/RouteGuide --objcgrpc_out=Pods/RouteGuide ../../protos/route_guide.proto
```

Running this command generates the following files under `Pods/RouteGuide/`:
- `RouteGuide.pbobjc.h`, the header which declares your generated message classes.
- `RouteGuide.pbobjc.m`, which contains the implementation of your message classes.
- `RouteGuide.pbrpc.h`, the header which declares your generated service classes.
- `RouteGuide.pbrpc.m`, which contains the implementation of your service classes.

These contain:
- All the protocol buffer code to populate, serialize, and retrieve our request and response message types.
- A class called `RTGRouteGuide` that lets clients call the methods defined in the `RouteGuide` service.

You can also use the provided Podspec file to generate client code from any other proto service definition; just replace the name (matching the file name), version, and other metadata.


<a name="client"></a>
## Creating the client

In this section, we'll look at creating an Objective-C client for our `RouteGuide` service. You can see our complete example client code in [grpc-common/objective-c/route_guide/ViewControllers.m](https://github.com/grpc/grpc-common/blob/master/objective-c/route_guide/ViewControllers.m). (Note: In your apps, for maintainability and readability reasons, you shouldn't put all of your view controllers in a single file; it's done here only to simplify the learning process).

### Constructing a client object

To call service methods, we first need to create a client object, an instance of the generated `RTGRouteGuide` class. The designated initializer of the class expects a `NSString *` with the server address and port we want to connect to:

```objective-c
#import <RouteGuide/RouteGuide.pbrpc.h>

static NSString * const kHostAddress = @"http://localhost:50051";

...

RTGRouteGuide *client = [[RTGRouteGuide alloc] initWithHost:kHostAddress];
```

Notice that we've specified the HTTP scheme in the host address. This is because the server we will be using to test our client doesn't use [TLS](http://en.wikipedia.org/wiki/Transport_Layer_Security). This is fine because it will be running locally on our development machine. The most common case, though, is connecting with a gRPC server on the internet, running gRPC over TLS. For that case, the HTTPS scheme can be specified (or no scheme at all, as HTTPS is the default value). The default value of the port is that of the scheme selected: 443 for HTTPS and 80 for HTTP.


### Calling service methods

Now let's look at how we call our service methods. As you will see, all these methods are asynchronous, so you can call them from the main thread of your app without worrying about freezing your UI or the OS killing your app.

#### Simple RPC

Calling the simple RPC `GetFeature` is nearly as straightforward as calling any other asynchronous method on Cocoa.

```objective-c
RTGPoint *point = [RTGPoint message];
point.latitude = 40E7;
point.longitude = -74E7;

[client getFeatureWithRequest:point handler:^(RTGFeature *response, NSError *error) {
  if (response) {
    // Successful response received
  } else {
    // RPC error
  }
}];
```

As you can see, we create and populate a request protocol buffer object (in our case `RTGPoint`). Then, we call the method on the client object, passing it the request, and a block to handle the response (or any RPC error). If the RPC finishes successfully, the handler block is called with a `nil` error argument, and we can read the response information from the server from the response argument. If, instead, some RPC error happens, the handler block is called with a `nil` response argument, and we can read the details of the problem from the error argument.

```objective-c
NSLog(@"Found feature called %@ at %@.", response.name, response.location);
```

#### Streaming RPCs

Now let's look at our streaming methods. Here's where we call the response-streaming method `ListFeatures`, which results in our client receiving a stream of geographical `RTGFeature`s:

```objective-c
[client listFeaturesWithRequest:rectangle handler:^(BOOL done, RTGFeature *response, NSError *error) {
  if (response) {
    // Element of the stream of responses received
  } else if (error) {
    // RPC error; the stream is over.
  }
  if (done) {
    // The stream is over (all the responses were received, or an error occured). Do any cleanup.
  }
}];
```

Notice how the signature of the handler block now includes a `BOOL done` parameter. The handler block can be called any number of times; only on the last call is the `done` argument value set to `YES`. If an error occurs, the RPC finishes and the handler is called with the arguments `(YES, nil, error)`.

The request-streaming method `RecordRoute` expects a stream of `RTGPoint`s from the cient. This stream is passed to the method as an object that conforms to the `GRXWriter` protocol. The simplest way to create one is to initialize one from a `NSArray` object:


```objective-c
#import <gRPC/GRXWriter+Immediate.h>

...

RTGPoint *point1 = [RTGPoint message];
point.latitude = 40E7;
point.longitude = -74E7;

RTGPoint *point2 = [RTGPoint message];
point.latitude = 40E7;
point.longitude = -74E7;

GRXWriter *locationsWriter = [GRXWriter writerWithContainer:@[point1, point2]];

[client recordRouteWithRequestsWriter:locationsWriter handler:^(RTGRouteSummary *response, NSError *error) {
  if (response) {
    NSLog(@"Finished trip with %i points", response.pointCount);
    NSLog(@"Passed %i features", response.featureCount);
    NSLog(@"Travelled %i meters", response.distance);
    NSLog(@"It took %i seconds", response.elapsedTime);
  } else {
    NSLog(@"RPC error: %@", error);
  }
}];

```

The `GRXWriter` protocol is generic enough to allow for asynchronous streams, streams of future values, or even infinite streams.

Finally, let's look at our bidirectional streaming RPC `RouteChat()`. The way to call a bidirectional streaming RPC is just a combination of how to call request-streaming RPCs and response-streaming RPCs.

```objective-c
[client routeChatWithRequestsWriter:notesWriter handler:^(BOOL done, RTGRouteNote *note, NSError *error) {
  if (note) {
    NSLog(@"Got message %@ at %@", note.message, note.location);
  } else if (error) {
    NSLog(@"RPC error: %@", error);
  }
  if (done) {
    NSLog(@"Chat ended.");
  }
}];
```

The semantics for the handler block and the `GRXWriter` argument here are exactly the same as for our request-streaming and response-streaming methods. Although both client and server will always get the other's messages in the order they were written, the two streams operate completely independently.
