#gRPC Basics: Python

This tutorial provides a basic Python programmer's introduction to working with gRPC. By walking through this example you'll learn how to:

- Define a service in a .proto file.
- Generate server and client code using the protocol buffer compiler.
- Use the Python gRPC API to write a simple client and server for your service.

It assumes that you have read the [Getting started](https://github.com/grpc/grpc-common) guide and are familiar with [protocol buffers] (https://developers.google.com/protocol-buffers/docs/overview). Note that the example in this tutorial uses the proto3 version of the protocol buffers language, which is currently in alpha release:you can find out more in the [proto3 language guide](https://developers.google.com/protocol-buffers/docs/proto3) and see the [release notes](https://github.com/google/protobuf/releases) for the new version in the protocol buffers Github repository.

This isn't a comprehensive guide to using gRPC in Python: more reference documentation is coming soon.


## Why use gRPC?

This example is a simple route mapping application that lets clients get information about features on their route, create a summary of their route, and exchange route information such as traffic updates with the server and other clients.

With gRPC you can define your service once in a .proto file and implement clients and servers in any of gRPC's supported languages, which in turn can be run in environments ranging from servers inside Google to your own tablet, with all the complexity of communication between different languages and environments is handled for you by gRPC. You also get all the advantages of working with protocol buffers, including efficient serialization, a simple IDL, and easy interface updating.

## Example code and setup

The example code for this tutorial is in [grpc/grpc-common/python/route_guide](https://github.com/grpc/grpc-common/tree/master/python/route_guide). To download the example, clone the `grpc-common` repository by running the following command:
```shell
$ git clone https://github.com/grpc/grpc-common.git
```

Then change your current directory to `grpc-common/python/route_guide`:
```shell
$ cd grpc-common/python/route_guide
```

You also should have the relevant tools installed to generate the server and client interface code - if you don't already, follow the setup instructions in [the Python quick start guide](https://github.com/grpc/grpc-common/tree/master/python).

## Defining the service

Your first step (as you'll know from [Getting started](https://github.com/grpc/grpc-common)) is to define the gRPC *service* and the method *request* and *response* types using [protocol buffers](https://developers.google.com/protocol-buffers/docs/overview). You can see the complete .proto file in [`grpc-common/protos/route_guide.proto`](https://github.com/grpc/grpc-common/blob/master/protos/route_guide.proto).

To define a service, you specify a named `service` in your .proto file:

```protobuf
service RouteGuide {
   // (Method definitions not shown)
}
```

Then you define `rpc` methods inside your service definition, specifying their request and response types. gRPC lets you define four kinds of service method, all of which are used in the `RouteGuide` service:

- A *simple RPC* where the client sends a request to the server using the stub and waits for a response to come back, just like a normal function call.
```protobuf
   // Obtains the feature at a given position.
   rpc GetFeature(Point) returns (Feature) {}
```

- A *response-streaming RPC* where the client sends a request to the server and gets a stream to read a sequence of messages back. The client reads from the returned stream until there are no more messages. As you can see in the example, you specify a response-streaming method by placing the `stream` keyword before the *response* type.
```protobuf
  // Obtains the Features available within the given Rectangle.  Results are
  // streamed rather than returned at once (e.g. in a response message with a
  // repeated field), as the rectangle may cover a large area and contain a
  // huge number of features.
  rpc ListFeatures(Rectangle) returns (stream Feature) {}
```

- A *request-streaming RPC* where the client writes a sequence of messages and sends them to the server, again using a provided stream. Once the client has finished writing the messages, it waits for the server to read them all and return its response. You specify a request-streaming method by placing the `stream` keyword before the *request* type.
```protobuf
  // Accepts a stream of Points on a route being traversed, returning a
  // RouteSummary when traversal is completed.
  rpc RecordRoute(stream Point) returns (RouteSummary) {}
```

- A *bidirectionally-streaming RPC* where both sides send a sequence of messages using a read-write stream. The two streams operate independently, so clients and servers can read and write in whatever order they like: for example, the server could wait to receive all the client messages before writing its responses, or it could alternately read a message then write a message, or some other combination of reads and writes. The order of messages in each stream is preserved. You specify this type of method by placing the `stream` keyword before both the request and the response.
```protobuf
  // Accepts a stream of RouteNotes sent while a route is being traversed,
  // while receiving other RouteNotes (e.g. from other users).
  rpc RouteChat(stream RouteNote) returns (stream RouteNote) {}
```

Your .proto file also contains protocol buffer message type definitions for all the request and response types used in our service methods - for example, here's the `Point` message type:
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

## Generating client and server code

Next you need to generate the gRPC client and server interfaces from your .proto service definition. You do this using the protocol buffer compiler `protoc` with a special gRPC Python plugin. Make sure you've installed protoc and followed the gRPC Python plugin [installation instructions](https://github.com/grpc/grpc/blob/master/INSTALL) first):

With `protoc` and the gRPC Python plugin installed, use the following command to generate the Python code:

```shell
$ protoc -I ../../protos --python_out=. --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_python_plugin` ../../protos/route_guide.proto
```

Note that as we've already provided a version of the generated code in the example repository, running this command regenerates the appropriate file rather than creates a new one. The generated code file is called `route_guide_pb2.py` and contains:
- classes for the messages defined in route_guide.proto
- abstract classes for the service defined in route_guide.proto
   - `EarlyAdopterRouteGuideServicer`, which defines the interface for implementations of the RouteGuide service
   - `EarlyAdopterRouteGuideServer`, which may be started and stopped
   - `EarlyAdopterRouteGuideStub`, which can be used by clients to invoke RouteGuide RPCs
- functions for application use
   - `early_adopter_create_RouteGuide_server`, which creates a gRPC server given an `EarlyAdopterRouteGuideServicer` object
   - `early_adopter_create_RouteGuide_stub`, which can be used by clients to create a stub object

<a name="server"></a>
## Creating the server

First let's look at how you create a `RouteGuide` server. If you're only interested in creating gRPC clients, you can skip this section and go straight to [Creating the client](#client) (though you might find it interesting anyway!).

Creating and running a `RouteGuide` server breaks down into two work items:
- Implementing the servicer interface generated from our service definition with functions that perform the actual "work" of the service.
- Running a gRPC server to listen for requests from clients and transmit responses.

You can find the example `RouteGuide` server in [grpc-common/python/route_guide/route_guide_server.py](https://github.com/grpc/grpc-common/blob/master/python/route_guide/route_guide_server.py).

### Implementing RouteGuide

`route_guide_server.py` has a `RouteGuideServicer` class that implements the generated interface `route_guide_pb2.EarlyAdopterRouteGuideServicer`:

```python
# RouteGuideServicer provides an implementation of the methods of the RouteGuide service.
class RouteGuideServicer(route_guide_pb2.EarlyAdopterRouteGuideServicer):
```

`RouteGuideServicer` implements all the `RouteGuide` service methods.

#### Simple RPC

Let's look at the simplest type first, `GetFeature`, which just gets a `Point` from the client and returns the corresponding feature information from its database in a `Feature`.

```python
  def GetFeature(self, request, context):
    feature = get_feature(self.db, request)
    if feature is None:
      return route_guide_pb2.Feature(name="", location=request)
    else:
      return feature
```

The method is passed a `route_guide_pb2.Point` request for the RPC, and an `RpcContext` object that provides RPC-specific information such as timeout limits. It returns a `route_guide_pb2.Feature` response.

#### Response-streaming RPC

Now let's look at the next method. `ListFeatures` is a response-streaming RPC that sends multiple `Feature`s to the client.

```python
  def ListFeatures(self, request, context):
    left = min(request.lo.longitude, request.hi.longitude)
    right = max(request.lo.longitude, request.hi.longitude)
    top = max(request.lo.latitude, request.hi.latitude)
    bottom = min(request.lo.latitude, request.hi.latitude)
    for feature in self.db:
      if (feature.location.longitude >= left and
          feature.location.longitude <= right and
          feature.location.latitude >= bottom and
          feature.location.latitude <= top):
        yield feature
```

Here the request message is a `route_guide_pb2.Rectangle` within which the client wants to find `Feature`s. Instead of returning a single response the method yields zero or more responses.

#### Request-streaming RPC

The request-streaming method `RecordRoute` uses an [iterator](https://docs.python.org/2/library/stdtypes.html#iterator-types) of request values and returns a single response value.

```python
  def RecordRoute(self, request_iterator, context):
    point_count = 0
    feature_count = 0
    distance = 0.0
    prev_point = None

    start_time = time.time()
    for point in request_iterator:
      point_count += 1
      if get_feature(self.db, point):
        feature_count += 1
      if prev_point:
        distance += get_distance(prev_point, point)
      prev_point = point

    elapsed_time = time.time() - start_time
    return route_guide_pb2.RouteSummary(point_count=point_count,
                                        feature_count=feature_count,
                                        distance=int(distance),
                                        elapsed_time=int(elapsed_time))
```

#### Bidirectional streaming RPC

Lastly let's look at the bidirectionally-streaming method `RouteChat`.

```python
  def RouteChat(self, request_iterator, context):
    prev_notes = []
    for new_note in request_iterator:
      for prev_note in prev_notes:
        if prev_note.location == new_note.location:
          yield prev_note
      prev_notes.append(new_note)
```

This method's semantics are a combination of those of the request-streaming method and the response-streaming method. It is passed an iterator of request values and is itself an iterator of response values.

### Starting the server

Once you have implemented all the `RouteGuide` methods, the next step is to start up a gRPC server so that clients can actually use your service:

```python
def serve():
  server = route_guide_pb2.early_adopter_create_RouteGuide_server(
      RouteGuideServicer(), 50051, None, None)
  server.start()
```

Because `start()` does not block you may need to sleep-loop if there is nothing else for your code to do while serving.

<a name="client"></a>
## Creating the client

You can see the complete example client code in [grpc-common/python/route_guide/route_guide_client.py](https://github.com/grpc/grpc-common/blob/master/python/route_guide/route_guide_client.py).

### Creating a stub

To call service methods, we first need to create a *stub*.

We use the `early_adopter_create_RouteGuide_stub` function of the `route_guide_pb2` module, generated from our .proto.

```python
stub = RouteGuide::Stub.new('localhost', 50051)
```

The returned object implements all the methods defined by the `EarlyAdopterRouteGuideStub` interface, and is also a [context manager](https://docs.python.org/2/library/stdtypes.html#typecontextmanager). All RPCs invoked on the stub must be invoked within the stub's context, so it is common for stubs to be created and used with a [with statement](https://docs.python.org/2/reference/compound_stmts.html#the-with-statement):

```python
with route_guide_pb2.early_adopter_create_RouteGuide_stub('localhost', 50051) as stub:
```

### Calling service methods

For RPC methods that return a single response ("response-unary" methods), gRPC Python supports both synchronous (blocking) and asynchronous (non-blocking) control flow semantics. For response-streaming RPC methods, calls immediately return an iterator of response values. Calls to that iterator's `next()` method block until the response to be yielded from the iterator becomes available.

#### Simple RPC

A synchronous call to the simple RPC `GetFeature` is nearly as straightforward as calling a local method. The RPC call waits for the server to respond, and will either return a response or raise an exception:

```python
feature = stub.GetFeature(point, timeout_in_seconds)
```

An asynchronous call to `GetFeature` is similar, but like calling a local method asynchronously in a thread pool:

```python
feature_future = stub.GetFeature.async(point, timeout_in_seconds)
feature = feature_future.result()
```

#### Response-streaming RPC

Calling the response-streaming `ListFeatures` is similar to working with sequence types:

```python
for feature in stub.ListFeatures(rectangle, timeout_in_seconds):
```

#### Request-streaming RPC

Calling the request-streaming `RecordRoute` is similar to passing a sequence to a local method. Like the simple RPC above that also returns a single response, it can be called synchronously or asynchronously:

```python
route_summary = stub.RecordRoute(point_sequence, timeout_in_seconds)
```

```python
route_summary_future = stub.RecordRoute.async(point_sequence, timeout_in_seconds)
route_summary = route_summary_future.result()
```

#### Bidirectional streaming RPC

Calling the bidirectionally-streaming `RouteChat` has (as is the case on the service-side) a combination of the request-streaming and response-streaming semantics:

```python
for received_route_note in stub.RouteChat(sent_routes, timeout_in_seconds):
```

## Try it out!

Run the server, which will listen on port 50051:

```shell
$ python route_guide_server.py
```

Run the client (in a different terminal):

```shell
$ python route_guide_client.py
```
