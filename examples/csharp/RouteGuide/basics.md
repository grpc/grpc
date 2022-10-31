This tutorial provides a basic C# programmer's introduction to working with gRPC.

By walking through this example you'll learn how to:

- Define a service in a .proto file.
- Generate server and client code using the protocol buffer compiler.
- Use the C# gRPC API to write a simple client and server for your service.

It assumes that you have read the [Introduction to gRPC](/docs/what-is-grpc/introduction/) and are familiar
with [protocol buffers](https://developers.google.com/protocol-buffers/docs/overview). Note that the
example in this tutorial uses the proto3 version of the protocol buffers
language: you can find out more in the
[proto3 language guide](https://developers.google.com/protocol-buffers/docs/proto3) and
[C# generated code reference](https://developers.google.com/protocol-buffers/docs/reference/csharp-generated).

### Why use gRPC?

Our example is a simple route mapping application that lets clients get information about features on their route, create a summary of their route, and exchange route information such as traffic updates with the server and other clients.

With gRPC we can define our service once in a `.proto` file and generate clients and servers in any of gRPC’s supported languages, which in turn can be run in environments ranging from servers inside a large data center to your own tablet — all the complexity of communication between different languages and environments is handled for you by gRPC. We also get all the advantages of working with protocol buffers, including efficient serialization, a simple IDL, and easy interface updating.

### Example code and setup

The example code for our tutorial is in
[grpc/grpc/examples/csharp/RouteGuide](https://github.com/grpc/grpc/tree/v1.46.x/examples/csharp/RouteGuide). To
download the example, clone the `grpc` repository by running the following
command:

```sh
$ git clone -b v1.46.x --depth 1 --shallow-submodules https://github.com/grpc/grpc
$ cd grpc
```

All the files for this tutorial are in the directory
`examples/csharp/RouteGuide`. Open the solution
`examples/csharp/RouteGuide/RouteGuide.sln` from Visual Studio (Windows or Mac) or Visual Studio Code.
For additional installation details, see the [How to use
instructions](https://github.com/grpc/grpc/tree/v1.46.x/src/csharp#how-to-use).

### Defining the service

Our first step (as you'll know from the [Introduction to gRPC](/docs/what-is-grpc/introduction/)) is to
define the gRPC *service* and the method *request* and *response* types using
[protocol buffers](https://developers.google.com/protocol-buffers/docs/overview).
You can see the complete .proto file in
[`examples/protos/route_guide.proto`](https://github.com/grpc/grpc/blob/v1.46.x/examples/protos/route_guide.proto).

To define a service, you specify a named `service` in your .proto file:

```protobuf
service RouteGuide {
   ...
}
```

Then you define `rpc` methods inside your service definition, specifying their
request and response types. gRPC lets you define four kinds of service method,
all of which are used in the `RouteGuide` service:

- A *simple RPC* where the client sends a request to the server using the client
  object and waits for a response to come back, just like a normal function
  call.

  ```protobuf
  // Obtains the feature at a given position.
  rpc GetFeature(Point) returns (Feature) {}
  ```

- A *server-side streaming RPC* where the client sends a request to the server
  and gets a stream to read a sequence of messages back. The client reads from
  the returned stream until there are no more messages. As you can see in our
  example, you specify a server-side streaming method by placing the `stream`
  keyword before the *response* type.

  ```protobuf
  // Obtains the Features available within the given Rectangle.  Results are
  // streamed rather than returned at once (e.g. in a response message with a
  // repeated field), as the rectangle may cover a large area and contain a
  // huge number of features.
  rpc ListFeatures(Rectangle) returns (stream Feature) {}
  ```

- A *client-side streaming RPC* where the client writes a sequence of messages
  and sends them to the server, again using a provided stream. Once the client
  has finished writing the messages, it waits for the server to read them all
  and return its response. You specify a client-side streaming method by placing
  the `stream` keyword before the *request* type.

  ```protobuf
  // Accepts a stream of Points on a route being traversed, returning a
  // RouteSummary when traversal is completed.
  rpc RecordRoute(stream Point) returns (RouteSummary) {}
  ```

- A *bidirectional streaming RPC* where both sides send a sequence of messages
  using a read-write stream. The two streams operate independently, so clients
  and servers can read and write in whatever order they like: for example, the
  server could wait to receive all the client messages before writing its
  responses, or it could alternately read a message then write a message, or
  some other combination of reads and writes. The order of messages in each
  stream is preserved. You specify this type of method by placing the `stream`
  keyword before both the request and the response.

  ```protobuf
  // Accepts a stream of RouteNotes sent while a route is being traversed,
  // while receiving other RouteNotes (e.g. from other users).
  rpc RouteChat(stream RouteNote) returns (stream RouteNote) {}
  ```

Our  `.proto` file also contains protocol buffer message type definitions for all
the request and response types used in our service methods - for example, here's
the `Point` message type:

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

### Generating client and server code

Next we need to generate the gRPC client and server interfaces from our .proto
service definition. This can be done by invoking the protocol buffer compiler `protoc` with
a special gRPC C# plugin from the command line, but starting from version
1.17 the `Grpc.Tools` NuGet package integrates with MSBuild to provide [automatic C# code generation](https://github.com/grpc/grpc/blob/v1.46.x/src/csharp/BUILD-INTEGRATION.md)
from `.proto` files, which gives much better developer experience by running
the right commands for you as part of the build.

This example already has a dependency on `Grpc.Tools` NuGet package and the
`route_guide.proto` has already been added to the project, so the only thing
needed to generate the client and server code is to build the solution.
That can be done by running `dotnet build RouteGuide.sln` or building directly
in Visual Studio.

The build regenerates the following files
under the `RouteGuide/obj/Debug/TARGET_FRAMEWORK` directory:

- `RouteGuide.cs` contains all the protocol buffer code to populate,
  serialize, and retrieve our request and response message types
- `RouteGuideGrpc.cs` provides generated client and server classes,
  including:
   - an abstract class `RouteGuide.RouteGuideBase` to inherit from when defining
     RouteGuide service implementations
   - a class `RouteGuide.RouteGuideClient` that can be used to access remote
     RouteGuide instances

### Creating the server {#server}

First let's look at how we create a `RouteGuide` server. If you're only
interested in creating gRPC clients, you can skip this section and go straight
to [Creating the client](#client) (though you might find it interesting
anyway!).

There are two parts to making our `RouteGuide` service do its job:

- Implementing the service functionality by inheriting from the base class
  generated from our service definition: doing the actual "work" of our service.
- Running a gRPC server to listen for requests from clients and return the
  service responses.

You can find our example `RouteGuide` server in
[examples/csharp/RouteGuide/RouteGuideServer/RouteGuideImpl.cs](https://github.com/grpc/grpc/blob/v1.46.x/examples/csharp/RouteGuide/RouteGuideServer/RouteGuideImpl.cs).
Let's take a closer look at how it works.

#### Implementing RouteGuide

As you can see, our server has a `RouteGuideImpl` class that inherits from the
generated `RouteGuide.RouteGuideBase`:

```csharp
// RouteGuideImpl provides an implementation of the RouteGuide service.
public class RouteGuideImpl : RouteGuide.RouteGuideBase
```

##### Simple RPC

`RouteGuideImpl` implements all our service methods. Let's look at the simplest
type first, `GetFeature`, which just gets a `Point` from the client and returns
the corresponding feature information from its database in a `Feature`.

```csharp
public override Task<Feature> GetFeature(Point request, Grpc.Core.ServerCallContext context)
{
    return Task.FromResult(CheckFeature(request));
}
```

The method is passed a context for the RPC (which is empty in the alpha
release), the client's `Point` protocol buffer request, and returns a `Feature`
protocol buffer. In the method we create the `Feature` with the appropriate
information, and then return it. To allow asynchronous implementation, the
method returns `Task<Feature>` rather than just `Feature`. You are free to
perform your computations synchronously and return the result once you've
finished, just as we do in the example.

##### Server-side streaming RPC

Now let's look at something a bit more complicated - a streaming RPC.
`ListFeatures` is a server-side streaming RPC, so we need to send back multiple
`Feature` protocol buffers to our client.

```csharp
// in RouteGuideImpl
public override async Task ListFeatures(Rectangle request,
    Grpc.Core.IServerStreamWriter<Feature> responseStream,
    Grpc.Core.ServerCallContext context)
{
    var responses = features.FindAll( (feature) => feature.Exists() && request.Contains(feature.Location) );
    foreach (var response in responses)
    {
        await responseStream.WriteAsync(response);
    }
}
```

As you can see, here the request object is a `Rectangle` in which our client
wants to find `Feature`s, but instead of returning a simple response we need to
write responses to an asynchronous stream `IServerStreamWriter` using async
method `WriteAsync`.

##### Client-side streaming RPC

Similarly, the client-side streaming method `RecordRoute` uses an
[IAsyncEnumerator](https://github.com/Reactive-Extensions/Rx.NET/blob/master/Ix.NET/Source/System.Interactive.Async/IAsyncEnumerator.cs),
to read the stream of requests using the async method `MoveNext` and the
`Current` property.

```csharp
public override async Task<RouteSummary> RecordRoute(Grpc.Core.IAsyncStreamReader<Point> requestStream,
    Grpc.Core.ServerCallContext context)
{
    int pointCount = 0;
    int featureCount = 0;
    int distance = 0;
    Point previous = null;
    var stopwatch = new Stopwatch();
    stopwatch.Start();

    while (await requestStream.MoveNext())
    {
        var point = requestStream.Current;
        pointCount++;
        if (CheckFeature(point).Exists())
        {
            featureCount++;
        }
        if (previous != null)
        {
            distance += (int) previous.GetDistance(point);
        }
        previous = point;
    }

    stopwatch.Stop();

    return new RouteSummary
    {
        PointCount = pointCount,
        FeatureCount = featureCount,
        Distance = distance,
        ElapsedTime = (int)(stopwatch.ElapsedMilliseconds / 1000)
    };
}
```

##### Bidirectional streaming RPC

Finally, let's look at our bidirectional streaming RPC `RouteChat`.

```csharp
public override async Task RouteChat(Grpc.Core.IAsyncStreamReader<RouteNote> requestStream,
    Grpc.Core.IServerStreamWriter<RouteNote> responseStream,
    Grpc.Core.ServerCallContext context)
{
    while (await requestStream.MoveNext())
    {
        var note = requestStream.Current;
        List<RouteNote> prevNotes = AddNoteForLocation(note.Location, note);
        foreach (var prevNote in prevNotes)
        {
            await responseStream.WriteAsync(prevNote);
        }
    }
}
```

Here the method receives both `requestStream` and `responseStream` arguments.
Reading the requests is done the same way as in the client-side streaming method
`RecordRoute`.  Writing the responses is done the same way as in the server-side
streaming method `ListFeatures`.

#### Starting the server

Once we've implemented all our methods, we also need to start up a gRPC server
so that clients can actually use our service. The following snippet shows how we
do this for our `RouteGuide` service:

```csharp
var features = RouteGuideUtil.LoadFeatures();

Server server = new Server
{
    Services = { RouteGuide.BindService(new RouteGuideImpl(features)) },
    Ports = { new ServerPort("localhost", Port, ServerCredentials.Insecure) }
};
server.Start();

Console.WriteLine("RouteGuide server listening on port " + port);
Console.WriteLine("Press any key to stop the server...");
Console.ReadKey();

server.ShutdownAsync().Wait();
```
As you can see, we build and start our server using `Grpc.Core.Server` class. To
do this, we:

1. Create an instance of `Grpc.Core.Server`.
1. Create an instance of our service implementation class `RouteGuideImpl`.
1. Register our service implementation by adding its service definition to the
   `Services` collection (We obtain the service definition from the generated
   `RouteGuide.BindService` method).
1. Specify the address and port we want to use to listen for client requests.
   This is done by adding `ServerPort` to the `Ports` collection.
1. Call `Start` on the server instance to start an RPC server for our service.

### Creating the client {#client}

In this section, we'll look at creating a C# client for our `RouteGuide`
service. You can see our complete example client code in
[examples/csharp/RouteGuide/RouteGuideClient/Program.cs](https://github.com/grpc/grpc/blob/v1.46.x/examples/csharp/RouteGuide/RouteGuideClient/Program.cs).

#### Creating a client object

To call service methods, we first need to create a client object (also referred
to as *stub* for other gRPC languages).

First, we need to create a gRPC client channel that will connect to gRPC server.
Then, we create an instance of the `RouteGuide.RouteGuideClient` class generated
from our .proto, passing the channel as an argument.

```csharp
Channel channel = new Channel("127.0.0.1:50052", ChannelCredentials.Insecure);
var client = new RouteGuide.RouteGuideClient(channel);

// YOUR CODE GOES HERE

channel.ShutdownAsync().Wait();
```

#### Calling service methods

Now let's look at how we call our service methods. gRPC C# provides asynchronous
versions of each of the supported method types. For convenience, gRPC C# also
provides a synchronous method stub, but only for simple (single request/single
response) RPCs.

##### Simple RPC

Calling the simple RPC `GetFeature` in a synchronous way is nearly as
straightforward as calling a local method.

```csharp
Point request = new Point { Latitude = 409146138, Longitude = -746188906 };
Feature feature = client.GetFeature(request);
```

As you can see, we create and populate a request protocol buffer object (in our
case `Point`), and call the desired method on the client object, passing it the
request. If the RPC finishes with success, the response protocol buffer (in our
case `Feature`) is returned. Otherwise, an exception of type `RpcException` is
thrown, indicating the status code of the problem.

Alternatively, if you are in an async context, you can call an asynchronous
version of the method and use the `await` keyword to await the result:

```csharp
Point request = new Point { Latitude = 409146138, Longitude = -746188906 };
Feature feature = await client.GetFeatureAsync(request);
```

##### Streaming RPCs

Now let's look at our streaming methods. If you've already read [Creating the
server](#server) some of this may look very familiar - streaming RPCs are
implemented in a similar way on both sides. The difference with respect to
simple call is that the client methods return an instance of a call object. This
provides access to request/response streams and/or the asynchronous result,
depending on the streaming type you are using.

Here's where we call the server-side streaming method `ListFeatures`, which has
the property `ReponseStream` of type `IAsyncEnumerator<Feature>`

```csharp
using (var call = client.ListFeatures(request))
{
    while (await call.ResponseStream.MoveNext())
    {
        Feature feature = call.ResponseStream.Current;
        Console.WriteLine("Received " + feature.ToString());
    }
}
```

The client-side streaming method `RecordRoute` is similar, except we use the
property `RequestStream` to write the requests one by one using `WriteAsync`,
and eventually signal that no more requests will be sent using `CompleteAsync`.
The method result can be obtained through the property `ResponseAsync`.

```csharp
using (var call = client.RecordRoute())
{
    foreach (var point in points)
    {
        await call.RequestStream.WriteAsync(point);
    }
    await call.RequestStream.CompleteAsync();

    RouteSummary summary = await call.ResponseAsync;
}
```

Finally, let's look at our bidirectional streaming RPC `RouteChat`. In this
case, we write the request to `RequestStream` and receive the responses from
`ResponseStream`. As you can see from the example, the streams are independent
of each other.

```csharp
using (var call = client.RouteChat())
{
    var responseReaderTask = Task.Run(async () =>
    {
        while (await call.ResponseStream.MoveNext())
        {
            var note = call.ResponseStream.Current;
            Console.WriteLine("Received " + note);
        }
    });

    foreach (RouteNote request in requests)
    {
        await call.RequestStream.WriteAsync(request);
    }
    await call.RequestStream.CompleteAsync();
    await responseReaderTask;
}
```

### Try it out!

Build the client and server:

Using Visual Studio (or Visual Studio For Mac)
: Open the solution `examples/csharp/RouteGuide/RouteGuide.sln` and select **Build**.

Using `dotnet` command line tool

: Run `dotnet build RouteGuide.sln` from the `examples/csharp/RouteGuide`
  directory. For additional instructions on building the gRPC example with the
  `dotnet` command line tool, see [Quick start](../Greeter/quickstart.md).

Run the server:

```sh
> cd RouteGuideServer
> dotnet run
```

From a different terminal, run the client:

```sh
> cd RouteGuideClient
> dotnet run
```

You can also run the server and client directly from Visual Studio.
