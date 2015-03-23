#gRPC Basics: Java

This tutorial provides a basic Java programmer's introduction to working with gRPC. By walking through this example you'll learn how to:

- Define a service in a .proto file.
- Generate server and client code using the protocol buffer compiler.
- Use the Java gRPC API to write a simple client and server for your service.

It assumes that you have read the [Getting started](https://github.com/grpc/grpc-common) guide and are familiar with [protocol buffers] (https://developers.google.com/protocol-buffers/docs/overview). Note that the example in this tutorial uses the [proto3](https://github.com/google/protobuf/releases) version of the protocol buffers language, which is currently in alpha release: you can see the [release notes](https://github.com/google/protobuf/releases) for the new version in the protocol buffers Github repository.

This isn't a comprehensive guide to using gRPC in Java: more reference documentation is coming soon.

## Why use gRPC?

Our example is a simple route mapping application that lets clients get information about features on their route, create a summary of their route, and exchange route information such as traffic updates with the server and other clients.

With gRPC we can define our service once in a .proto file and implement clients and servers in any of gRPC's supported languages, which in turn can be run in environments ranging from servers inside Google to your own tablet - all the complexity of communication between different languages and environments is handled for you by gRPC. We also get all the advantages of working with protocol buffers, including efficient serialization, a simple IDL, and easy interface updating.

## Example code and setup

The example code for our tutorial is in [grpc/grpc-java/examples/src/main/java/io/grpc/examples](https://github.com/grpc/grpc-java/tree/master/examples/src/main/java/io/grpc/examples). To download the example, clone the `grpc-java` repository by running the following command:
```shell
$ git clone https://github.com/grpc/grpc-java.git
```

Then change your current directory to `grpc-java/examples`:
```shell
$ cd grpc-java/examples
```

You also should have the relevant tools installed to generate the server and client interface code - if you don't already, follow the setup instructions in [the Java quick start guide](https://github.com/grpc/grpc-common/tree/master/java).


## Defining the service

Our first step (as you'll know from [Getting started](https://github.com/grpc/grpc-common)) is to define the gRPC *service* and the method *request* and *response* types using [protocol buffers] (https://developers.google.com/protocol-buffers/docs/overview). You can see the complete .proto file in [`grpc-java/examples/src/main/proto/route_guide.proto`](https://github.com/grpc/grpc-java/blob/master/examples/src/main/proto/route_guide.proto).

As we're generating Java code in this example, we've specified a `java_package` file option in our .proto:
```proto
option java_package = "io.grpc.examples";
```

This specifies the package we want to use for our generated Java classes. If no explicit `java_package` option is given in the .proto file, then by default the proto package (specified using the "package" keyword) will be used. However, proto packages generally do not make good Java packages since proto packages are not expected to start with reverse domain names. If we generate code in another language from this .proto, the `java_package` option has no effect.

To define a service, we specify a named `service` in the .proto file:

```proto
service RouteGuide {
   ...
}
```

Then we define `rpc` methods inside our service definition, specifying their request and response types. gRPC lets you define four kinds of service method, all of which are used in the `RouteGuide` service:

- A *simple RPC* where the client sends a request to the server using the stub and waits for a response to come back, just like a normal function call.
```proto
   // Obtains the feature at a given position.
   rpc GetFeature(Point) returns (Feature) {}
```

- A *server-side streaming RPC* where the client sends a request to the server and gets a stream to read a sequence of messages back. The client reads from the returned stream until there are no more messages. As you can see in our example, you specify a server-side streaming method by placing the `stream` keyword before the *response* type.
```proto
  // Obtains the Features available within the given Rectangle.  Results are
  // streamed rather than returned at once (e.g. in a response message with a
  // repeated field), as the rectangle may cover a large area and contain a
  // huge number of features.
  rpc ListFeatures(Rectangle) returns (stream Feature) {}
```

- A *client-side streaming RPC* where the client writes a sequence of messages and sends them to the server, again using a provided stream. Once the client has finished writing the messages, it waits for the server to read them all and return its response. You specify a server-side streaming method by placing the `stream` keyword before the *request* type.
```proto
  // Accepts a stream of Points on a route being traversed, returning a
  // RouteSummary when traversal is completed.
  rpc RecordRoute(stream Point) returns (RouteSummary) {}
```

- A *bidirectional streaming RPC* where both sides send a sequence of messages using a read-write stream. The two streams operate independently, so clients and servers can read and write in whatever order they like: for example, the server could wait to receive all the client messages before writing its responses, or it could alternately read a message then write a message, or some other combination of reads and writes. The order of messages in each stream is preserved. You specify this type of method by placing the `stream` keyword before both the request and the response.
```proto
  // Accepts a stream of RouteNotes sent while a route is being traversed,
  // while receiving other RouteNotes (e.g. from other users).
  rpc RouteChat(stream RouteNote) returns (stream RouteNote) {}
```

Our .proto file also contains protocol buffer message type definitions for all the request and response types used in our service methods - for example, here's the `Point` message type:
```proto
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

Next we need to generate the gRPC client and server interfaces from our .proto service definition. We do this using the protocol buffer compiler `protoc` with a special gRPC Java plugin. You need to use the [proto3](https://github.com/google/protobuf/releases) compiler in order to generate gRPC services

For simplicity, we've provided a [Gradle build file](https://github.com/grpc/grpc-java/blob/master/examples/build.gradle) that runs `protoc` for you with the appropriate plugin, input, and output (if you want to run this yourself, make sure you've installed protoc and followed the gRPC code [installation instructions](https://github.com/grpc/grpc-java) first):

```shell
../gradlew build
```

which actually runs:

```shell
protoc -I examples/src/main/proto -I examples/build/extracted-protos/main --java_out=examples/build/generated-sources/main --java_plugin_out=examples/build/generated-sources/main --plugin=protoc-gen-java_plugin=compiler/build/binaries/java_pluginExecutable/java_plugin examples/src/main/proto/route_guide.proto
```

Running this command generates the following files:
- `RouteGuideOuterClass.java`, which contains all the protocol buffer code to populate, serialize, and retrieve our request and response message types
- `RouteGuideGrpc.java` which contains (along with some other useful code):
   - an interface for `RouteGuide` servers to implement, `RouteGuideGrpc.Service`, with all the methods defined in the `RouteGuide` service.
   - *stub* classes that clients can use to talk to a `RouteGuide` server. These also implement the `RouteGuide` interface.


<a name="server"></a>
## Creating the server

First let's look at how we create a `RouteGuide` server. If you're only interested in creating gRPC clients, you can skip this section and go straight to [Creating the client](#client) (though you might find it interesting anyway!).

There are two parts to making our `RouteGuide` service do its job:
- Implementing the service interface generated from our service definition: doing the actual "work" of our service.
- Running a gRPC server to listen for requests from clients and return the service responses.

You can find our example `RouteGuide` server in [grpc-java/examples/src/main/java/io/grpc/examples/RouteGuideServer.java](https://github.com/grpc/grpc-java/blob/master/examples/src/main/java/io/grpc/examples/RouteGuideServer.java). Let's take a closer look at how it works.

### Implementing RouteGuide

As you can see, our server has a `RouteGuideService` class that implements the generated `RouteGuideGrpc.Service` interface:

```java
private static class RouteGuideService implements RouteGuideGrpc.RouteGuide {
...
}
```
#### Simple RPC
`RouteGuideService` implements all our service methods. Let's look at the simplest type first, `GetFeature`, which just gets a `Point` from the client and returns the corresponding feature information from its database in a `Feature`.

```java
    @Override
    public void getFeature(Point request, StreamObserver<Feature> responseObserver) {
      responseObserver.onValue(getFeature(request));
      responseObserver.onCompleted();
    }

...

    private Feature getFeature(Point location) {
      for (Feature feature : features) {
        if (feature.getLocation().getLatitude() == location.getLatitude()
            && feature.getLocation().getLongitude() == location.getLongitude()) {
          return feature;
        }
      }

      // No feature was found, return an unnamed feature.
      return Feature.newBuilder().setName("").setLocation(location).build();
    }
```

`getFeature()` takes two parameters:
- `Point`: the request
- `StreamObserver<Feature>`: a response observer, which is a special interface for the server to call with its response.

To return our response to the client and complete the call:

1. We construct and populate a `Feature` response object to return to the client, as specified in our service definition. In this example, we do this in a separate private `getFeature()` method.
2. We use the response observer's `onValue()` method to return the `Feature`.
3. We use the response observer's `onCompleted()` method to specify that we've finished dealing with the RPC.

#### Server-side streaming RPC
Next let's look at one of our streaming RPCs. `ListFeatures` is a server-side streaming RPC, so we need to send back multiple `Feature`s to our client.

```java
private final Collection<Feature> features;

...

    @Override
    public void listFeatures(Rectangle request, StreamObserver<Feature> responseObserver) {
      int left = min(request.getLo().getLongitude(), request.getHi().getLongitude());
      int right = max(request.getLo().getLongitude(), request.getHi().getLongitude());
      int top = max(request.getLo().getLatitude(), request.getHi().getLatitude());
      int bottom = min(request.getLo().getLatitude(), request.getHi().getLatitude());

      for (Feature feature : features) {
        if (!RouteGuideUtil.exists(feature)) {
          continue;
        }

        int lat = feature.getLocation().getLatitude();
        int lon = feature.getLocation().getLongitude();
        if (lon >= left && lon <= right && lat >= bottom && lat <= top) {
          responseObserver.onValue(feature);
        }
      }
      responseObserver.onCompleted();
    }
```

Like the simple RPC, this method gets a request object (the `Rectangle` in which our client wants to find `Feature`s) and a `StreamObserver` response observer.

This time, we get as many `Feature` objects as we need to return to the client (in this case, we select them from the service's feature collection based on whether they're inside our request `Rectangle`), and write them each in turn to the response observer using its `Write()` method. Finally, as in our simple RPC, we use the response observer's `onCompleted()` method to tell gRPC that we've finished writing responses.

#### Client-side streaming RPC
Now let's look at something a little more complicated: the client-side streaming method `RecordRoute`, where we get a stream of `Point`s from the client and return a single `RouteSummary` with information about their trip.

```java
 @Override
    public StreamObserver<Point> recordRoute(final StreamObserver<RouteSummary> responseObserver) {
      return new StreamObserver<Point>() {
        int pointCount;
        int featureCount;
        int distance;
        Point previous;
        long startTime = System.nanoTime();

        @Override
        public void onValue(Point point) {
          pointCount++;
          if (RouteGuideUtil.exists(getFeature(point))) {
            featureCount++;
          }
          // For each point after the first, add the incremental distance from the previous point to
          // the total distance value.
          if (previous != null) {
            distance += calcDistance(previous, point);
          }
          previous = point;
        }

        @Override
        public void onError(Throwable t) {
          logger.log(Level.WARNING, "Encountered error in recordRoute", t);
        }

        @Override
        public void onCompleted() {
          long seconds = NANOSECONDS.toSeconds(System.nanoTime() - startTime);
          responseObserver.onValue(RouteSummary.newBuilder().setPointCount(pointCount)
              .setFeatureCount(featureCount).setDistance(distance)
              .setElapsedTime((int) seconds).build());
          responseObserver.onCompleted();
        }
      };
    }
```

As you can see, like the previous method types our method gets a `StreamObserver` response observer parameter, but this time it returns a `StreamObserver` for the client to write its `Point`s. 

In the method body we instantiate an anonymous `StreamObserver` to return, in which we:
- Override the `onValue()` method to get features and other information each time the client writes a `Point` to the message stream.
- Override the `onCompleted()` method (called when the *client* has finished writing messages) to populate and build our `RouteSummary`. We then call our method's own response observer's `onValue()` with our `RouteSummary`, and then call its `onCompleted()` method to finish the call from the server side.

#### Bidirectional streaming RPC
Finally, let's look at our bidirectional streaming RPC `RouteChat()`.

```cpp
    @Override
    public StreamObserver<RouteNote> routeChat(final StreamObserver<RouteNote> responseObserver) {
      return new StreamObserver<RouteNote>() {
        @Override
        public void onValue(RouteNote note) {
          List<RouteNote> notes = getOrCreateNotes(note.getLocation());

          // Respond with all previous notes at this location.
          for (RouteNote prevNote : notes.toArray(new RouteNote[0])) {
            responseObserver.onValue(prevNote);
          }

          // Now add the new note to the list
          notes.add(note);
        }

        @Override
        public void onError(Throwable t) {
          logger.log(Level.WARNING, "Encountered error in routeChat", t);
        }

        @Override
        public void onCompleted() {
          responseObserver.onCompleted();
        }
      };
    }
```

As with our client-side streaming example, we both get and return a `StreamObserver` response observer, except this time we return values via our method's response observer while the client is still writing messages to *their* message stream. The syntax for reading and writing here is exactly the same as for our client-streaming and server-streaming methods. Although each side will always get the other's messages in the order they were written, both the client and server can read and write in any order — the streams operate completely independently.

### Starting the server

Once we've implemented all our methods, we also need to start up a gRPC server so that clients can actually use our service. The following snippet shows how we do this for our `RouteGuide` service:

```java
  public void start() {
    gRpcServer = NettyServerBuilder.forPort(port)
        .addService(RouteGuideGrpc.bindService(new RouteGuideService(features)))
        .build().start();
    logger.info("Server started, listening on " + port);
    ...
  }
```
As you can see, we build and start our server using a `NettyServerBuilder`. This is a builder for servers based on the [Netty](http://netty.io/) transport framework. 

To do this, we:

1. Create an instance of our service implementation class `RouteGuideService` and pass it to the generated `RouteGuideGrpc` class's static `bindService()` method to get a service definition.
3. Specify the address and port we want to use to listen for client requests using the builder's `forPort()` method.
4. Register our service implementation with the builder by passing the service definition returned from `bindService()` to the builder's `addService()` method.
5. Call `build()` and `start()` on the builder to create and start an RPC server for our service.

<a name="client"></a>
## Creating the client

In this section, we'll look at creating a Java client for our `RouteGuide` service. You can see our complete example client code in [grpc-java/examples/src/main/java/io/grpc/examples/RouteGuideClient.java](https://github.com/grpc/grpc-java/blob/master/examples/src/main/java/io/grpc/examples/RouteGuideClient.java).

### Creating a stub

To call service methods, we first need to create a *stub*, or rather, two stubs: 
- a *blocking/synchronous* stub: this means that the RPC call waits for the server to respond, and will either return a response or raise an exception.
- a *non-blocking/asynchronous* stub that makes non-blocking calls to the server, where the response is returned asynchronously. You can make certain types of streaming call only using the asynchronous stub.

First we need to create a gRPC *channel* for our stub, specifying the server address and port we want to connect to:

```java
 channel = NettyChannelBuilder.forAddress(host, port)
        .negotiationType(NegotiationType.PLAINTEXT)
        .build();
```

As with our server, we're using the [Netty](http://netty.io/) transport framework, so we use a `NettyChannelBuilder`.

Now we can use the channel to create our stubs using the `newStub` and `newBlockingStub` methods provided in the `RouteGuideGrpc` class we generated from our .proto.

```java
    blockingStub = RouteGuideGrpc.newBlockingStub(channel);
    asyncStub = RouteGuideGrpc.newStub(channel);
```

### Calling service methods

Now let's look at how we call our service methods. 

#### Simple RPC

Calling the simple RPC `GetFeature` on the blocking stub is as straightforward as calling a local method.

```java
      Point request = Point.newBuilder().setLatitude(lat).setLongitude(lon).build();
      Feature feature = blockingStub.getFeature(request);
```

We create and populate a request protocol buffer object (in our case `Point`), pass it to the `getFeature()` method on our blocking stub, and get back a `Feature`.

#### Server-side streaming RPC

Next, let's look at a server-side streaming call to `ListFeatures`, which returns a stream of geographical `Feature`s:

```java
      Rectangle request =
          Rectangle.newBuilder()
              .setLo(Point.newBuilder().setLatitude(lowLat).setLongitude(lowLon).build())
              .setHi(Point.newBuilder().setLatitude(hiLat).setLongitude(hiLon).build()).build();
      Iterator<Feature> features = blockingStub.listFeatures(request);
```

As you can see, it's very similar to the simple RPC we just looked at, except instead of returning a single `Feature`, the method returns an `Iterator` that the client can use to read all the returned `Feature`s.

#### Client-side streaming RPC

Now for something a little more complicated: the client-side streaming method `RecordRoute`, where we send a stream of `Point`s to the server and get back a single `RouteSummary`. For this method we need to use the asynchronous stub. If you've already read [Creating the server](#server) some of this may look very familiar - asynchronous streaming RPCs are implemented in a similar way on both sides.

```java
  public void recordRoute(List<Feature> features, int numPoints) throws Exception {
    info("*** RecordRoute");
    final SettableFuture<Void> finishFuture = SettableFuture.create();
    StreamObserver<RouteSummary> responseObserver = new StreamObserver<RouteSummary>() {
      @Override
      public void onValue(RouteSummary summary) {
        info("Finished trip with {0} points. Passed {1} features. "
            + "Travelled {2} meters. It took {3} seconds.", summary.getPointCount(),
            summary.getFeatureCount(), summary.getDistance(), summary.getElapsedTime());
      }

      @Override
      public void onError(Throwable t) {
        finishFuture.setException(t);
      }

      @Override
      public void onCompleted() {
        finishFuture.set(null);
      }
    };

    StreamObserver<Point> requestObserver = asyncStub.recordRoute(responseObserver);
    try {
      // Send numPoints points randomly selected from the features list.
      StringBuilder numMsg = new StringBuilder();
      Random rand = new Random();
      for (int i = 0; i < numPoints; ++i) {
        int index = rand.nextInt(features.size());
        Point point = features.get(index).getLocation();
        info("Visiting point {0}, {1}", RouteGuideUtil.getLatitude(point),
            RouteGuideUtil.getLongitude(point));
        requestObserver.onValue(point);
        // Sleep for a bit before sending the next one.
        Thread.sleep(rand.nextInt(1000) + 500);
        if (finishFuture.isDone()) {
          break;
        }
      }
      info(numMsg.toString());
      requestObserver.onCompleted();

      finishFuture.get();
      info("Finished RecordRoute");
    } catch (Exception e) {
      requestObserver.onError(e);
      logger.log(Level.WARNING, "RecordRoute Failed", e);
      throw e;
    }
  }
```

As you can see, to call this method we need to create a `StreamObserver`, which implements a special interface for the server to call with its `RouteSummary` response. In our `StreamObserver` we:
- Override the `onValue()` method to print out the returned information when the server writes a `RouteSummary` to the message stream.
- Override the `onCompleted()` method (called when the *server* has completed the call on its side) to set a `SettableFuture` that we can check to see if the server has finished writing.

We then pass the `StreamObserver` to the asynchronous stub's `recordRoute()` method and get back our own `StreamObserver` request observer to write our `Point`s to send to the server.  Once we've finished writing points, we use the request observer's `onCompleted()` method to tell gRPC that we've finished writing on the client side. Once we're done, we check our `SettableFuture` to check that the server has completed on its side.

#### Bidirectional streaming RPC

Finally, let's look at our bidirectional streaming RPC `RouteChat()`.

```java
  public void routeChat() throws Exception {
    info("*** RoutChat");
    final SettableFuture<Void> finishFuture = SettableFuture.create();
    StreamObserver<RouteNote> requestObserver =
        asyncStub.routeChat(new StreamObserver<RouteNote>() {
          @Override
          public void onValue(RouteNote note) {
            info("Got message \"{0}\" at {1}, {2}", note.getMessage(), note.getLocation()
                .getLatitude(), note.getLocation().getLongitude());
          }

          @Override
          public void onError(Throwable t) {
            finishFuture.setException(t);
          }

          @Override
          public void onCompleted() {
            finishFuture.set(null);
          }
        });

    try {
      RouteNote[] requests =
          {newNote("First message", 0, 0), newNote("Second message", 0, 1),
              newNote("Third message", 1, 0), newNote("Fourth message", 1, 1)};

      for (RouteNote request : requests) {
        info("Sending message \"{0}\" at {1}, {2}", request.getMessage(), request.getLocation()
            .getLatitude(), request.getLocation().getLongitude());
        requestObserver.onValue(request);
      }
      requestObserver.onCompleted();

      finishFuture.get();
      info("Finished RouteChat");
    } catch (Exception t) {
      requestObserver.onError(t);
      logger.log(Level.WARNING, "RouteChat Failed", t);
      throw t;
    }
  }
```

As with our client-side streaming example, we both get and return a `StreamObserver` response observer, except this time we send values via our method's response observer while the server is still writing messages to *their* message stream. The syntax for reading and writing here is exactly the same as for our client-streaming method. Although each side will always get the other's messages in the order they were written, both the client and server can read and write in any order — the streams operate completely independently.


## Try it out!

Follow the instructions in the example directory [README](https://github.com/grpc/grpc-java/blob/master/examples/README.md) to build and run the client and server.



