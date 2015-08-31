#gRPC Basics: Ruby

This tutorial provides a basic Ruby programmer's introduction to working with gRPC. By walking through this example you'll learn how to:

- Define a service in a .proto file.
- Generate server and client code using the protocol buffer compiler.
- Use the Ruby gRPC API to write a simple client and server for your service.

It assumes that you have read the [Getting started](https://github.com/grpc/grpc/tree/master/examples) guide and are familiar with [protocol buffers] (https://developers.google.com/protocol-buffers/docs/overview). Note that the example in this tutorial uses the proto3 version of the protocol buffers language, which is currently in alpha release:you can find out more in the [proto3 language guide](https://developers.google.com/protocol-buffers/docs/proto3) and see the [release notes](https://github.com/google/protobuf/releases) for the new version in the protocol buffers Github repository.

This isn't a comprehensive guide to using gRPC in Ruby: more reference documentation is coming soon.

## Why use gRPC?

Our example is a simple route mapping application that lets clients get information about features on their route, create a summary of their route, and exchange route information such as traffic updates with the server and other clients.

With gRPC we can define our service once in a .proto file and implement clients and servers in any of gRPC's supported languages, which in turn can be run in environments ranging from servers inside Google to your own tablet - all the complexity of communication between different languages and environments is handled for you by gRPC. We also get all the advantages of working with protocol buffers, including efficient serialization, a simple IDL, and easy interface updating.

## Example code and setup

The example code for our tutorial is in [examples/ruby/route_guide](.). To download the example, clone this repository by running the following command:
```shell
$ git clone https://github.com/grpc/grpc.git
```

Then change your current directory to `examples/ruby/route_guide`:
```shell
$ cd examples/ruby/route_guide
```

You also should have the relevant tools installed to generate the server and client interface code - if you don't already, follow the setup instructions in [the Ruby quick start guide](..).


## Defining the service

Our first step (as you'll know from [Getting started](https://github.com/grpc/grpc/tree/master/examples)) is to define the gRPC *service* and the method *request* and *response* types using [protocol buffers] (https://developers.google.com/protocol-buffers/docs/overview). You can see the complete .proto file in [`examples/protos/route_guide.proto`](../../route_guide.proto).

To define a service, you specify a named `service` in your .proto file:

```protobuf
service RouteGuide {
   ...
}
```

Then you define `rpc` methods inside your service definition, specifying their request and response types. gRPC lets you define four kinds of service method, all of which are used in the `RouteGuide` service:

- A *simple RPC* where the client sends a request to the server using the stub and waits for a response to come back, just like a normal function call.
```protobuf
   // Obtains the feature at a given position.
   rpc GetFeature(Point) returns (Feature) {}
```

- A *server-side streaming RPC* where the client sends a request to the server and gets a stream to read a sequence of messages back. The client reads from the returned stream until there are no more messages. As you can see in our example, you specify a server-side streaming method by placing the `stream` keyword before the *response* type.
```protobuf
  // Obtains the Features available within the given Rectangle.  Results are
  // streamed rather than returned at once (e.g. in a response message with a
  // repeated field), as the rectangle may cover a large area and contain a
  // huge number of features.
  rpc ListFeatures(Rectangle) returns (stream Feature) {}
```

- A *client-side streaming RPC* where the client writes a sequence of messages and sends them to the server, again using a provided stream. Once the client has finished writing the messages, it waits for the server to read them all and return its response. You specify a server-side streaming method by placing the `stream` keyword before the *request* type.
```protobuf
  // Accepts a stream of Points on a route being traversed, returning a
  // RouteSummary when traversal is completed.
  rpc RecordRoute(stream Point) returns (RouteSummary) {}
```

- A *bidirectional streaming RPC* where both sides send a sequence of messages using a read-write stream. The two streams operate independently, so clients and servers can read and write in whatever order they like: for example, the server could wait to receive all the client messages before writing its responses, or it could alternately read a message then write a message, or some other combination of reads and writes. The order of messages in each stream is preserved. You specify this type of method by placing the `stream` keyword before both the request and the response.
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


## Generating client and server code

Next we need to generate the gRPC client and server interfaces from our .proto service definition. We do this using the protocol buffer compiler `protoc` with a special gRPC Ruby plugin.

If you want to run this yourself, make sure you've installed protoc and followed the gRPC Ruby plugin [installation instructions](https://github.com/grpc/grpc/blob/master/INSTALL) first):

Once that's done, the following command can be used to generate the ruby code.

```shell
$ protoc -I ../../protos --ruby_out=lib --grpc_out=lib --plugin=protoc-gen-grpc=`which grpc_ruby_plugin` ../../protos/route_guide.proto
```

Running this command regenerates the following files in the lib directory:
- `lib/route_guide.pb` defines a module `Examples::RouteGuide`
  - This contain all the protocol buffer code to populate, serialize, and retrieve our request and response message types
- `lib/route_guide_services.pb`, extends `Examples::RouteGuide` with stub and service classes
   - a class `Service` for use as a base class when defining RouteGuide service implementations
   - a class `Stub` that can be used to access remote RouteGuide instances


<a name="server"></a>
## Creating the server

First let's look at how we create a `RouteGuide` server. If you're only interested in creating gRPC clients, you can skip this section and go straight to [Creating the client](#client) (though you might find it interesting anyway!).

There are two parts to making our `RouteGuide` service do its job:
- Implementing the service interface generated from our service definition: doing the actual "work" of our service.
- Running a gRPC server to listen for requests from clients and return the service responses.

You can find our example `RouteGuide` server in [route_guide_server.rb](route_guide_server.rb). Let's take a closer look at how it works.

### Implementing RouteGuide

As you can see, our server has a `ServerImpl` class that extends the generated `RouteGuide::Service`:

```ruby
# ServerImpl provides an implementation of the RouteGuide service.
class ServerImpl < RouteGuide::Service
```

`ServerImpl` implements all our service methods. Let's look at the simplest type first, `GetFeature`, which just gets a `Point` from the client and returns the corresponding feature information from its database in a `Feature`.

```ruby
  def get_feature(point, _call)
    name = @feature_db[{
      'longitude' => point.longitude,
      'latitude' => point.latitude }] || ''
    Feature.new(location: point, name: name)
  end
```

The method is passed a _call for the RPC, the client's `Point` protocol buffer request, and returns a `Feature` protocol buffer. In the method we create the `Feature` with the appropriate information, and then `return` it.

Now let's look at something a bit more complicated - a streaming RPC. `ListFeatures` is a server-side streaming RPC, so we need to send back multiple `Feature`s to our client.

```ruby
# in ServerImpl

  def list_features(rectangle, _call)
    RectangleEnum.new(@feature_db, rectangle).each
  end
```

As you can see, here the request object is a `Rectangle` in which our client wants to find `Feature`s, but instead of returning a simple response we need to return an [Enumerator](http://ruby-doc.org//core-2.2.0/Enumerator.html) that yields the responses. In the method, we use a helper class `RectangleEnum`, to act as an Enumerator implementation.

Similarly, the client-side streaming method `record_route` uses an [Enumerable](http://ruby-doc.org//core-2.2.0/Enumerable.html), but here it's obtained from the call object, which we've ignored in the earlier examples.  `call.each_remote_read` yields each message sent by the client in turn.

```ruby
  call.each_remote_read do |point|
    ...
  end
```
Finally, let's look at our bidirectional streaming RPC `route_chat`.

```ruby
  def route_chat(notes)
    q = EnumeratorQueue.new(self)
    t = Thread.new do
      begin
        notes.each do |n|
		...
      end
	end
    q = EnumeratorQueue.new(self)
  ...
    return q.each_item
  end 
```

Here the method receives an [Enumerable](http://ruby-doc.org//core-2.2.0/Enumerable.html), but also returns an [Enumerator](http://ruby-doc.org//core-2.2.0/Enumerator.html) that yields the responses.  The implementation demonstrates how to set these up so that the requests and responses can be handled concurrently.  Although each side will always get the other's messages in the order they were written, both the client and server can read and write in any order — the streams operate completely independently.

### Starting the server

Once we've implemented all our methods, we also need to start up a gRPC server so that clients can actually use our service. The following snippet shows how we do this for our `RouteGuide` service:

```ruby
  s = GRPC::RpcServer.new
  s.add_http2_port(port)
  logger.info("... running insecurely on #{port}")
  s.handle(ServerImpl.new(feature_db))
  s.run
```
As you can see, we build and start our server using a `GRPC::RpcServer`. To do this, we:

1. Create an instance of our service implementation class `ServerImpl`.
2. Specify the address and port we want to use to listen for client requests using the builder's `add_http2_port` method.
3. Register our service implementation with the `GRPC::RpcServer`.
4. Call `run` on the`GRPC::RpcServer` to create and start an RPC server for our service.

<a name="client"></a>
## Creating the client

In this section, we'll look at creating a Ruby client for our `RouteGuide` service. You can see our complete example client code in [route_guide_client.rb](route_guide_client.rb).

### Creating a stub

To call service methods, we first need to create a *stub*.

We use the `Stub` class of the `RouteGuide` module generated from our .proto.

```ruby
 stub = RouteGuide::Stub.new('localhost:50051')
```

### Calling service methods

Now let's look at how we call our service methods. Note that the gRPC Ruby only provides  *blocking/synchronous* versions of each method: this means that the RPC call waits for the server to respond, and will either return a response or raise an exception.

#### Simple RPC

Calling the simple RPC `GetFeature` is nearly as straightforward as calling a local method.

```ruby
GET_FEATURE_POINTS = [
  Point.new(latitude:  409_146_138, longitude: -746_188_906),
  Point.new(latitude:  0, longitude: 0)
]
..
  GET_FEATURE_POINTS.each do |pt|
    resp = stub.get_feature(pt)
	...
    p "- found '#{resp.name}' at #{pt.inspect}"
  end
```

As you can see, we create and populate a request protocol buffer object (in our case `Point`), and create a response protocol buffer object for the server to fill in.  Finally, we call the method on the stub, passing it the context, request, and response. If the method returns `OK`, then we can read the response information from the server from our response object.


#### Streaming RPCs

Now let's look at our streaming methods. If you've already read [Creating the server](#server) some of this may look very familiar - streaming RPCs are implemented in a similar way on both sides. Here's where we call the server-side streaming method `list_features`, which returns an `Enumerable` of `Features`

```ruby
  resps = stub.list_features(LIST_FEATURES_RECT)
  resps.each do |r|
    p "- found '#{r.name}' at #{r.location.inspect}"
  end
```

The client-side streaming method `record_route` is similar, except there we pass the server an `Enumerable`.

```ruby
  ...
  reqs = RandomRoute.new(features, points_on_route)
  resp = stub.record_route(reqs.each, deadline)
  ...
```

Finally, let's look at our bidirectional streaming RPC `route_chat`. In this case, we pass `Enumerable` to the method and get back an `Enumerable`.

```ruby
  resps = stub.route_chat(ROUTE_CHAT_NOTES)
  resps.each { |r| p "received #{r.inspect}" }
```

Although it's not shown well by this example, each enumerable is independent of the other - both the client and server can read and write in any order — the streams operate completely independently.

## Try it out!

Build client and server:

```shell
$ # from examples/ruby
$ gem install bundler && bundle install
```
Run the server, which will listen on port 50051:
```shell
$ # from examples/ruby
$ bundle exec route_guide/route_guide_server.rb ../node/route_guide/route_guide_db.json &
```
Run the client (in a different terminal):
```shell
$ # from examples/ruby
$ bundle exec route_guide/route_guide_client.rb ../node/route_guide/route_guide_db.json &
```

