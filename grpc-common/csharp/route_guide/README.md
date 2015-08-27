#gRPC Basics: C# #

This tutorial provides a basic C# programmer's introduction to working with gRPC. By walking through this example you'll learn how to:

- Define a service in a .proto file.
- Generate server and client code using the protocol buffer compiler.
- Use the C# gRPC API to write a simple client and server for your service.

It assumes that you have read the [Getting started](https://github.com/grpc/grpc-common) guide and are familiar with [protocol buffers] (https://developers.google.com/protocol-buffers/docs/overview). Note that the example in this tutorial only uses the proto2 version of the protocol buffers language, as proto3 support for C# is not ready yet (see [protobuf C# README](https://github.com/google/protobuf/tree/master/csharp#proto2--proto3)).

This isn't a comprehensive guide to using gRPC in C#: more reference documentation is coming soon.

## Why use gRPC?

Our example is a simple route mapping application that lets clients get information about features on their route, create a summary of their route, and exchange route information such as traffic updates with the server and other clients.

With gRPC we can define our service once in a .proto file and implement clients and servers in any of gRPC's supported languages, which in turn can be run in environments ranging from servers inside Google to your own tablet - all the complexity of communication between different languages and environments is handled for you by gRPC. We also get all the advantages of working with protocol buffers, including efficient serialization, a simple IDL, and easy interface updating.

## Example code and setup

The example code for our tutorial is in [grpc/grpc-common/csharp/route_guide](https://github.com/grpc/grpc-common/tree/master/csharp/route_guide). To download the example, clone the `grpc-common` repository by running the following command:
```shell
$ git clone https://github.com/google/grpc-common.git
```

All the files for this tutorial are in the directory  `grpc-common/csharp/route_guide`.
Open the solution `grpc-common/csharp/route_guide/RouteGuide.sln` from Visual Studio (or Monodevelop on Linux).

On Windows, you should not need to do anything besides opening the solution. All the needed dependencies will be restored
for you automatically by the `Grpc` NuGet package upon building the solution.

On Linux (or MacOS), you will first need to install protobuf and gRPC C Core using Linuxbrew (or Homebrew) tool in order to be 
able to generate the server and client interface code and run the examples. Follow the instructions for [Linux](https://github.com/grpc/grpc/tree/master/src/csharp#usage-linux-mono) or [MacOS](https://github.com/grpc/grpc/tree/master/src/csharp#usage-macos-mono).

## Defining the service

Our first step (as you'll know from [Getting started](https://github.com/grpc/grpc-common)) is to define the gRPC *service* and the method *request* and *response* types using [protocol buffers] (https://developers.google.com/protocol-buffers/docs/overview). You can see the complete .proto file in [`grpc-common/csharp/route_guide/RouteGuide/protos/route_guide.proto`](https://github.com/grpc/grpc-common/blob/master/sharp/route_guide/RouteGuide/protos/route_guide.proto).

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

Next we need to generate the gRPC client and server interfaces from our .proto service definition. We do this using the protocol buffer compiler `protoc` with a special gRPC C# plugin.

If you want to run this yourself, make sure you've installed protoc and gRPC C# plugin. The instructions vary based on your OS:
- For Windows, the `Grpc.Tools` NuGet package contains the binaries you will need to generate the code.
- For Linux, make sure you've [installed gRPC C Core using Linuxbrew](https://github.com/grpc/grpc/tree/master/src/csharp#usage-linux-mono)
- For MacOS, make sure you've [installed gRPC C Core using Homebrew](https://github.com/grpc/grpc/tree/master/src/csharp#usage-macos-mono)

Once that's done, the following command can be used to generate the C# code.

To generate the code on Windows, we use `protoc.exe` and `grpc_csharp_plugin.exe` binaries that are shipped with the `Grpc.Tools` NuGet package under the `tools` directory.
Normally you would need to add the `Grpc.Tools` package to the solution yourself, but in this tutorial it has been already done for you. Following command should be run from the `csharp/route_guide` directory:
```
> packages\Grpc.Tools.0.5.1\tools\protoc -I RouteGuide/protos --csharp_out=RouteGuide --grpc_out=RouteGuide --plugin=protoc-gen-grpc=packages\Grpc.Tools.0.5.1\tools\grpc_csharp_plugin.exe RouteGuide/protos/route_guide.proto
```

On Linux/MacOS, we rely on `protoc` and `grpc_csharp_plugin` being installed by Linuxbrew/Homebrew. Run this command from the route_guide directory:
```shell
$ protoc -I RouteGuide/protos --csharp_out=RouteGuide --grpc_out=RouteGuide --plugin=protoc-gen-grpc=`which grpc_csharp_plugin` RouteGuide/protos/route_guide.proto
```

Running one of the previous commands regenerates the following files in the RouteGuide directory:
- `RouteGuide/RouteGuide.cs` defines a namespace `examples`
  - This contains all the protocol buffer code to populate, serialize, and retrieve our request and response message types
- `RouteGuide/RouteGuideGrpc.cs`, provides stub and service classes
   - an interface `RouteGuide.IRouteGuide` to inherit from when defining RouteGuide service implementations
   - a class `RouteGuide.RouteGuideClient` that can be used to access remote RouteGuide instances


<a name="server"></a>
## Creating the server

First let's look at how we create a `RouteGuide` server. If you're only interested in creating gRPC clients, you can skip this section and go straight to [Creating the client](#client) (though you might find it interesting anyway!).

There are two parts to making our `RouteGuide` service do its job:
- Implementing the service interface generated from our service definition: doing the actual "work" of our service.
- Running a gRPC server to listen for requests from clients and return the service responses.

You can find our example `RouteGuide` server in [grpc-common/csharp/route_guide/RouteGuideServer/RouteGuideImpl.cs](https://github.com/grpc/grpc-common/blob/master/csharp/route_guide/RouteGuideServer/RouteGuideServerImpl.cs). Let's take a closer look at how it works.

### Implementing RouteGuide

As you can see, our server has a `RouteGuideImpl` class that implements the generated `RouteGuide.IRouteGuide`:

```csharp
// RouteGuideImpl provides an implementation of the RouteGuide service.
public class RouteGuideImpl : RouteGuide.IRouteGuide
```

#### Simple RPC

`RouteGuideImpl` implements all our service methods. Let's look at the simplest type first, `GetFeature`, which just gets a `Point` from the client and returns the corresponding feature information from its database in a `Feature`.

```csharp
    public Task<Feature> GetFeature(Grpc.Core.ServerCallContext context, Point request)
    {
        return Task.FromResult(CheckFeature(request));
    }
```

The method is passed a context for the RPC (which is empty in the alpha release), the client's `Point` protocol buffer request, and returns a `Feature` protocol buffer. In the method we create the `Feature` with the appropriate information, and then return it. To allow asynchronous
implementation, the method returns `Task<Feature>` rather than just `Feature`. You are free to perform your computations synchronously and return
the result once you've finished, just as we do in the example.

#### Server-side streaming RPC

Now let's look at something a bit more complicated - a streaming RPC. `ListFeatures` is a server-side streaming RPC, so we need to send back multiple `Feature` protocol buffers to our client.

```csharp
    // in RouteGuideImpl
    public async Task ListFeatures(Grpc.Core.ServerCallContext context, Rectangle request,
	    Grpc.Core.IServerStreamWriter<Feature> responseStream)
    {
        int left = Math.Min(request.Lo.Longitude, request.Hi.Longitude);
        int right = Math.Max(request.Lo.Longitude, request.Hi.Longitude);
        int top = Math.Max(request.Lo.Latitude, request.Hi.Latitude);
        int bottom = Math.Min(request.Lo.Latitude, request.Hi.Latitude);

        foreach (var feature in features)
        {
            if (!RouteGuideUtil.Exists(feature))
            {
                continue;
            }

            int lat = feature.Location.Latitude;
            int lon = feature.Location.Longitude;
            if (lon >= left && lon <= right && lat >= bottom && lat <= top)
            {
                await responseStream.WriteAsync(feature);
            }
        }
    }
```

As you can see, here the request object is a `Rectangle` in which our client wants to find `Feature`s, but instead of returning a simple response we need to write responses to an asynchronous stream `IServerStreamWriter` using async method `WriteAsync`.

#### Client-side streaming RPC

Similarly, the client-side streaming method `RecordRoute` uses an [IAsyncEnumerator](https://github.com/Reactive-Extensions/Rx.NET/blob/master/Ix.NET/Source/System.Interactive.Async/IAsyncEnumerator.cs), to read the stream of requests using the async method `MoveNext` and the `Current` property.

```csharp
    public async Task<RouteSummary> RecordRoute(Grpc.Core.ServerCallContext context,
	    Grpc.Core.IAsyncStreamReader<Point> requestStream)
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
            if (RouteGuideUtil.Exists(CheckFeature(point)))
            {
                featureCount++;
            }
            if (previous != null)
            {
                distance += (int) CalcDistance(previous, point);
            }
            previous = point;
        }

        stopwatch.Stop();
        return RouteSummary.CreateBuilder().SetPointCount(pointCount)
            .SetFeatureCount(featureCount).SetDistance(distance)
            .SetElapsedTime((int) (stopwatch.ElapsedMilliseconds / 1000)).Build();
    }
```

#### Bidirectional streaming RPC

Finally, let's look at our bidirectional streaming RPC `RouteChat`.

```csharp
    public async Task RouteChat(Grpc.Core.ServerCallContext context,
	    Grpc.Core.IAsyncStreamReader<RouteNote> requestStream, Grpc.Core.IServerStreamWriter<RouteNote> responseStream)
    {
        while (await requestStream.MoveNext())
        {
            var note = requestStream.Current;
            List<RouteNote> notes = GetOrCreateNotes(note.Location);

			List<RouteNote> prevNotes;
            lock (notes)
            {
                prevNotes = new List<RouteNote>(notes);
            }

            foreach (var prevNote in prevNotes)
            {
                await responseStream.WriteAsync(prevNote);
            }                
                
            lock (notes)
            {
                notes.Add(note);
            }
        }
    }
```

Here the method receives both `requestStream` and `responseStream` arguments.  Reading the requests is done the same way as in the client-side streaming method `RecordRoute`.  Writing the responses is done the same way as in the server-side streaming method `ListFeatures`.

### Starting the server

Once we've implemented all our methods, we also need to start up a gRPC server so that clients can actually use our service. The following snippet shows how we do this for our `RouteGuide` service:

```csharp
var features = RouteGuideUtil.ParseFeatures(RouteGuideUtil.DefaultFeaturesFile);
GrpcEnvironment.Initialize();

Server server = new Server();
server.AddServiceDefinition(RouteGuide.BindService(new RouteGuideImpl(features)));
int port = server.AddListeningPort("localhost", 50052);
server.Start();

Console.WriteLine("RouteGuide server listening on port " + port);
Console.WriteLine("Press any key to stop the server...");
Console.ReadKey();

server.ShutdownAsync().Wait();
GrpcEnvironment.Shutdown();
```
As you can see, we build and start our server using `Grpc.Core.Server` class. To do this, we:

1. Create an instance of `Grpc.Core.Server`.
1. Create an instance of our service implementation class `RouteGuideImpl`.
3. Register our service implementation with the server using the `AddServiceDefinition` method and the generated method `RouteGuide.BindService`.
2. Specify the address and port we want to use to listen for client requests using the `AddListeningPort` method.
4. Call `Start` on the server instance to start an RPC server for our service.

<a name="client"></a>
## Creating the client

In this section, we'll look at creating a C# client for our `RouteGuide` service. You can see our complete example client code in [grpc-common/csharp/route_guide/RouteGuideClient/Program.cs](https://github.com/grpc/grpc-common/blob/master/csharp/route_guide/RouteGuideClient/Program.cs).

### Creating a stub

To call service methods, we first need to create a *stub*.

First, we need to create a gRPC client channel that will connect to gRPC server. Then, we use the `RouteGuide.NewStub` method of the `RouteGuide` class generated from our .proto.

```csharp
GrpcEnvironment.Initialize();

using (Channel channel = new Channel("127.0.0.1:50052"))
{
    var client = RouteGuide.NewStub(channel);
 
    // YOUR CODE GOES HERE
}

GrpcEnvironment.Shutdown();
```

### Calling service methods

Now let's look at how we call our service methods. gRPC C# provides asynchronous versions of each of the supported method types. For convenience,
gRPC C# also provides a synchronous method stub, but only for simple (single request/single response) RPCs.

#### Simple RPC

Calling the simple RPC `GetFeature` in a synchronous way is nearly as straightforward as calling a local method.

```csharp
Point request = Point.CreateBuilder().SetLatitude(409146138).SetLongitude(-746188906).Build();
Feature feature = client.GetFeature(request);
```

As you can see, we create and populate a request protocol buffer object (in our case `Point`), and call the desired method on the client object, passing it the request. If the RPC finishes with success, the response protocol buffer (in our case `Feature`) will be returned. Otherwise, an exception of type `RpcException` will be thrown, indicating the status code of the problem.

Alternatively, if you are in async context, you can call an asynchronous version of the method (and use `await` keyword to await the result):
```csharp
Point request = Point.CreateBuilder().SetLatitude(409146138).SetLongitude(-746188906).Build();
Feature feature = await client.GetFeatureAsync(request);
```

#### Streaming RPCs

Now let's look at our streaming methods. If you've already read [Creating the server](#server) some of this may look very familiar - streaming RPCs are implemented in a similar way on both sides. The difference with respect to simple call is that the client methods return an instance of a call object, that provides access to request/response streams and/or asynchronous result (depending on the streaming type you are using).

Here's where we call the server-side streaming method `ListFeatures`, which has property `ReponseStream` of type `IAsyncEnumerator<Feature>`

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

The client-side streaming method `RecordRoute` is similar, except we use the property `RequestStream` to write the requests one by one using `WriteAsync` and eventually signal that no more request will be send using `CompleteAsync`. The method result can be obtained through the property
`Result`.
```csharp
using (var call = client.RecordRoute())
{
    foreach (var point in points)
	{
        await call.RequestStream.WriteAsync(point);
    }
    await call.RequestStream.CompleteAsync();

    RouteSummary summary = await call.Result;
}
```

Finally, let's look at our bidirectional streaming RPC `RouteChat`. In this case, we write the request to `RequestStream` and receive the responses from `ResponseStream`. As you can see from the example, the streams are independent of each other. 

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

## Try it out!

Build client and server:

Open the solution `grpc-common/csharp/route_guide/RouteGuide.sln` from Visual Studio (or Monodevelop on Linux) and hit "Build".

Run the server, which will listen on port 50052:
```
> cd RouteGuideServer/bin/Debug
> RouteGuideServer.exe
```

Run the client (in a different terminal):
```
> cd RouteGuideClient/bin/Debug
> RouteGuideClient.exe
```

You can also run the server and client directly from Visual Studio.

On Linux or Mac, use `mono RouteGuideServer.exe` and `mono RouteGuideClient.exe` to run the server and client.
