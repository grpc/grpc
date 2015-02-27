#gRPC Basics: Go

This tutorial provides a basic Go programmer's introduction to working with gRPC. By walking through this example you'll learn how to:

- Define a service in a .proto file.
- Generate server and client code using the protocol buffer compiler.
- Use the Go gRPC API to write a simple client and server for your service.

It assumes that you have read the [Getting started](https://github.com/grpc/grpc-common) guide and are familiar with [protocol buffers] (https://developers.google.com/protocol-buffers/docs/overview). Note that the example in this tutorial uses the proto3 version of the protocol buffers language, which is currently in alpha release: you can see the [release notes](https://github.com/google/protobuf/releases) for the new version in the protocol buffers Github repository.

This isn't a comprehensive guide to using gRPC in Go: more reference documentation is coming soon.

## Why use gRPC?

Our example is a simple route mapping application that lets clients get information about features on their route, create a summary of their route, and exchange route information such as traffic updates with the server and other clients.

With gRPC we can define our service once in a .proto file and implement clients and servers in any of gRPC's supported languages, which in turn can be run in environments ranging from servers inside Google to your own tablet - all the complexity of communication between different languages and environments is handled for you by gRPC. We also get all the advantages of working with protocol buffers, including efficient serialization, a simple IDL, and easy interface updating.

## Example code and setup

The example code for our tutorial is in [grpc/grpc-go/examples/route_guide](https://github.com/grpc/grpc-go/tree/master/examples/route_guide). To download the example, clone the `grpc-go` repository by running the following command:
```shell
$ go get google.golang.org/grpc
```

Then change your current directory to `grpc-go/examples/route_guide`:
```shell
$ cd $GOPATH/src/google.golang.org/grpc/examples/route_guide
```

You also should have the relevant tools installed to generate the server and client interface code - if you don't already, follow the setup instructions in [the Go quick start guide](https://github.com/grpc/grpc-common/tree/master/go).


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

- A *server-side streaming RPC* where the client sends a request to the server and gets a stream to read a sequence of messages back. The client reads from the returned stream until there are no more messages. As you can see in our example, you specify a server-side streaming method by placing the `stream` keyword before the *response* type.
```
  // Obtains the Features available within the given Rectangle.  Results are
  // streamed rather than returned at once (e.g. in a response message with a
  // repeated field), as the rectangle may cover a large area and contain a
  // huge number of features.
  rpc ListFeatures(Rectangle) returns (stream Feature) {}
```

- A *client-side streaming RPC* where the client writes a sequence of messages and sends them to the server, again using a provided stream. Once the client has finished writing the messages, it waits for the server to read them all and return its response. You specify a server-side streaming method by placing the `stream` keyword before the *request* type.
```
  // Accepts a stream of Points on a route being traversed, returning a
  // RouteSummary when traversal is completed.
  rpc RecordRoute(stream Point) returns (RouteSummary) {}
```

- A *bidirectional streaming RPC* where both sides send a sequence of messages using a read-write stream. The two streams operate independently, so clients and servers can read and write in whatever order they like: for example, the server could wait to receive all the client messages before writing its responses, or it could alternately read a message then write a message, or some other combination of reads and writes. The order of messages in each stream is preserved. You specify this type of method by placing the `stream` keyword before both the request and the response.
```
  // Accepts a stream of RouteNotes sent while a route is being traversed,
  // while receiving other RouteNotes (e.g. from other users).
  rpc RouteChat(stream RouteNote) returns (stream RouteNote) {}
```

Our .proto file also contains protocol buffer message type definitions for all the request and response types used in our service methods - for example, here's the `Point` message type:
```
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

Next we need to generate the gRPC client and server interfaces from our .proto service definition. We do this using the protocol buffer compiler `protoc` with a special gRPC Go plugin.

For simplicity, we've provided a [bash script](https://github.com/grpc/grpc-go/blob/master/codegen.sh) that runs `protoc` for you with the appropriate plugin, input, and output (if you want to run this by yourself, make sure you've installed protoc and followed the gRPC-Go [installation instructions](https://github.com/grpc/grpc-go/blob/master/README.md) first):

```shell
$ codegen.sh route_guide.proto
```

which actually runs:

```shell
$ protoc --go_out=plugins=grpc:. route_guide.proto
```

Running this command generates the following files in your current directory:
- `route_guide.pb.go`

This contains:
- All the protocol buffer code to populate, serialize, and retrieve our request and response message types
- An interface type (or *stub*) for clients to call with the methods defined in the `RouteGuide` service.
- An interface type for servers to implement, also with the methods defined in the `RouteGuide` service.


<a name="server"></a>
## Creating the server

First let's look at how we create a `RouteGuide` server. If you're only interested in creating gRPC clients, you can skip this section and go straight to [Creating the client](#client) (though you might find it interesting anyway!).

There are two parts to make our `RouteGuide` service do its job:
- Implementing the service interface generated from our service definition: doing the actual "work" of our service.
- Running a gRPC server to listen for requests from clients and dispatch them to the right service implementation.

You can find our example `RouteGuide` server in [grpc-go/examples/route_guide/server/server.go](https://github.com/grpc/grpc-go/tree/master/examples/route_guide/server/server.go). Let's take a closer look at how it works.

### Implementing RouteGuide

As you can see, our server has a `routeGuideServer` struct type that implements the generated `RouteGuideServer` interface:

```go
type routeGuideServer struct {
        ...
}
...

func (s *routeGuideServer) GetFeature(ctx context.Context, point *pb.Point) (*pb.Feature, error) {
        ...
}
...

func (s *routeGuideServer) ListFeatures(rect *pb.Rectangle, stream pb.RouteGuide_ListFeaturesServer) error {
        ...
}
...

func (s *routeGuideServer) RecordRoute(stream pb.RouteGuide_RecordRouteServer) error {
        ...
}
...

func (s *routeGuideServer) RouteChat(stream pb.RouteGuide_RouteChatServer) error {
        ...
}
...
```

`routeGuideServer` implements all our service methods. Let's look at the simplest type first, `GetFeature`, which just gets a `Point` from the client and returns the corresponding feature information from its database in a `Feature`.

```go
func (s *routeGuideServer) GetFeature(ctx context.Context, point *pb.Point) (*pb.Feature, error) {
	for _, feature := range s.savedFeatures {
		if proto.Equal(feature.Location, point) {
			return feature, nil
		}
	}
	// No feature was found, return an unnamed feature
	return &pb.Feature{"", point}, nil
}
```

The method is passed a context object for the RPC, the client's `Point` protocol buffer request. It returns a `Feature` protocol buffer object with the response information and an error. In the method we populate the `Feature` with the appropriate information, and then `return` it along with an `nil` error to tell gRPC that we've finished dealing with the RPC and that the `Feature` can be returned to the client.

Now let's look at something a bit more complicated - a streaming RPC. `ListFeatures` is a server-side streaming RPC, so we need to send back multiple `Feature`s to our client.

```go
func (s *routeGuideServer) ListFeatures(rect *pb.Rectangle, stream pb.RouteGuide_ListFeaturesServer) error {
	for _, feature := range s.savedFeatures {
		if inRange(feature.Location, rect) {
			if err := stream.Send(feature); err != nil {
				return err
			}
		}
	}
	return nil
}
```

As you can see, instead of getting simple request and response objects in our method parameters, this time we get a request object (the `Rectangle` in which our client wants to find `Feature`s) and a special `RouteGuide_ListFeaturesServer` object. In the method, we populate as many `Feature` objects as we need to return, writing them to the `RouteGuide_ListFeaturesServer` using its `Send()` method. Finally, as in our simple RPC, we return a `nil` error to tell gRPC that we've finished writing responses. Should there be any error happened in this call, we return a non-`nil` error and the gRPC layer will translate it into an appropriate RPC status to be sent on the wire.

If you look at the client-side streaming method `RecordRoute` you'll see it's quite similar, except this time we don't have to pass the method a request. Instead, we pass in a `RouteGuide_RecordRouteServer`, through which we can receive client messages using its `Recv()` method.

We use the `RouteGuide_RecordRouteServer`s `Recv()` method to repeatedly read in our client's requests to a request object (in this case a `Point`) until there are no more messages: the server needs to check the the error returned from `Read()` after each call. If `nil`, the stream is still good and it can continue reading; if it's `io.EOF` the message stream has ended. Otherwise, we return the error as is so that it'll be translated to an RPC status by the gRPC layer.

```go
for {
        point, err := stream.Recv()
        if err == io.EOF {
                return stream.SendAndClose(&pb.RouteSummary{
                        ...
                })
        }
        if err != nil {
                return err
        }
        ...
}
```
Finally, let's look at our bidirectional streaming RPC `RouteChat()`.

```go
func (s *routeGuideServer) RouteChat(stream pb.RouteGuide_RouteChatServer) error {
	for {
		in, err := stream.Recv()
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return err
		}
		key := serialize(in.Location)
                ... // look for notes to be sent to client
		for _, note := range s.routeNotes[key] {
			if err := stream.Send(note); err != nil {
				return err
			}
		}
	}
}
```

This time we get a `RouteGuide_RouteChatServer` that can be used to read *and* write messages. The syntax for reading and writing here is exactly the same as for our client-streaming and server-streaming methods. Although each side will always get the other's messages in the order they were written, both the client and server can read and write in any order — the streams operate completely independently.

### Starting the server

Once we've implemented all our methods, we also need to start up a gRPC server so that clients can actually use our service. The following snippet shows how we do this for our `RouteGuide` service:

```go
flag.Parse()
lis, err := net.Listen("tcp", fmt.Sprintf(":%d", *port))
if err != nil {
        log.Fatalf("failed to listen: %v", err)
}
grpcServer := grpc.NewServer()
pb.RegisterRouteGuideServer(grpcServer, &routeGuideServer{})
... // determine whether to use TLS
grpcServer.Serve(lis)
```
As you can see, we build our server using `grpc.NewServer()`. To do this, we:

1. Specify the port we want to use to listen for client requests using `lis, err := net.Listen("tcp", fmt.Sprintf(":%d", *port))`.
2. Create an instance of the gRPC server, by `grpc.NewServer()`.
3. Register our service implementation with the gRPC server.
4. Call `Serve()` on the server to do a blocking wait until process is killed or `Stop()` is called.

<a name="client"></a>
## Creating the client

In this section, we'll look at creating a Go client for our `RouteGuide` service. You can see our complete example client code in [grpc-go/examples/route_guide/client/client.go](https://github.com/grpc/grpc-go/tree/master/examples/route_guide/server/client.go).

### Creating a stub

To call service methods, we first need to create a gRPC "channel* as the message communication media by simply passing the server address and port number as follows:

```go
conn, err := grpc.Dial(*serverAddr)
defer conn.Close()
```

We can use DialOptions to set the auth credentials (e.g., TLS, GCE crednetials, JWT credentials) in grpc.Dial if the service you request requires that.

Once the gRPC *channel* is setup, we need a client *stub* to perform RPCs by using the `NewRouteGuideClient` method provided in the `pb` package we generated from our .proto.

```go
client := pb.NewRouteGuideClient(conn)
```

### Calling service methods

Now let's look at how we call our service methods. Note that in gRPC-Go, RPC operates in a blocking/synchronous mode, which means that the RPC call waits for the server to respond, and will either return a response or an error.

#### Simple RPC

Calling the simple RPC `GetFeature` is nearly as straightforward as calling a local method.

```go
printFeature(client, &pb.Point{409146138, -746188906})
...

func printFeature(client pb.RouteGuideClient, point *pb.Point) {
	log.Printf("Getting feature for point (%d, %d)", point.Latitude, point.Longitude)
	feature, err := client.GetFeature(context.Background(), point)
	if err != nil {
		log.Fatalf("%v.GetFeatures(_) = _, %v: ", client, err)
		return
	}
	log.Println(feature)
}

```

As you can see, we create and populate a request protocol buffer object (in our case `Point`). We also pass a `context.Context` object which allows us to time-out/cancel an RPC in flight. Finally, we call the method on the stub, passing it the context, and request. If the call doesn't return an error, then we can read the response information from the server from the first return value.

```go
log.Println(feature)
```

#### Streaming RPCs

Now let's look at our streaming methods. If you've already read [Creating the server](#server) some of this may look very familiar - streaming RPCs are implemented in a similar way on both sides. Here's where we call the server-side streaming method `ListFeatures`, which returns a stream of geographical `Feature`s:

```go
func printFeatures(client pb.RouteGuideClient, rect *pb.Rectangle) {
        stream, err := client.ListFeatures(context.Background(), rect)
        if err != nil {
                log.Fatalf("%v.ListFeatures(_) = _, %v", client, err)
        }
        for {
                feature, err := stream.Recv()
                if err == io.EOF {
                        break
                }
                if err != nil {
                        log.Fatalf("%v.ListFeatures(_) = _, %v", client, err)
                }
                log.Println(feature)
        }
```

As in simple RPC, we pass the method a context and a request. But instead of getting a response object back, we get back an instance of `RouteGuide_ListFeaturesClient`. The client can use the `RouteGuide_ListFeaturesClient` to read the server's responses. We use the `RouteGuide_ListFeaturesClient`'s `Recv()` method to repeatedly read in the server's responses to a response protocol buffer object (in this case a `Feature`) until there are no more messages: the client needs to check the error `err` returned from `Recv()` after each call. If `nil`, the stream is still good and it can continue reading; if it's `io.EOF` then the message stream has ended; otherwise there must be an RPC error, which is passed over through `err`.

The client-side streaming method `RecordRoute` is similar, except that we only pass the method a context and get a `RouteGuide_RecordRouteClient` back, which has a `Send()` method that we can use to send requests to the server.

```go
func runRecordRoute(client pb.RouteGuideClient) {
	// Create a random number of random points
	r := rand.New(rand.NewSource(time.Now().UnixNano()))
	pointCount := int(r.Int31n(100)) + 2 // Traverse at least two points
	var points []*pb.Point
	for i := 0; i < pointCount; i++ {
		points = append(points, randomPoint(r))
	}
	log.Printf("Traversing %d points.", len(points))
	stream, err := client.RecordRoute(context.Background())
	if err != nil {
		log.Fatalf("%v.RecordRoute(_) = _, %v", client, err)
	}
	for _, point := range points {
		if err := stream.Send(point); err != nil {
			log.Fatalf("%v.Send(%v) = %v", stream, point, err)
		}
	}
	reply, err := stream.CloseAndRecv()
	if err != nil {
		log.Fatalf("%v.CloseAndRecv() got error %v, want %v", stream, err, nil)
	}
	log.Printf("Route summary: %v", reply)
}
```

Once we've finished writing our client's requests to the stream using `Send()`, we need to call `CloseAndRecv()` on the stream to let gRPC know that we've finished writing and are expecting to receive a response. We get our RPC status from the `err` returned from `CloseAndRecv()`. If the status is `nil`, then the first return value will be a valid server response.

Finally, let's look at our bidirectional streaming RPC `RouteChat()`. As in the case of `RecordRoute`, we only pass the method a context object. But in this case we get back a `RouteGuide_RouteChatClient`, which we can use to both write *and* read messages.

```go
stream, err := client.RouteChat(context.Background())
```

The syntax for reading and writing here is exactly the same as for our client-streaming and server-streaming methods. Although each side will always get the other's messages in the order they were written, both the client and server can read and write in any order — the streams operate completely independently.

## Try it out!

To compile and run the server, assuming you are in the folder
`$GOPATH/src/google.golang.org/grpc/examples/route_guide`, simply:

```sh
$ go run server/server.go
```

Likewise, to run the client:

```sh
$ go run client/client.go
```

