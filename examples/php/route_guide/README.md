#gRPC Basics: PHP

This tutorial provides a basic PHP programmer's introduction to working with gRPC. By walking through this example you'll learn how to:

- Define a service in a .proto file.
- Generate client code using the protocol buffer compiler.
- Use the PHP gRPC API to write a simple client for your service.

It assumes a passing familiarity with [protocol buffers](https://developers.google.com/protocol-buffers/docs/overview). Note that the example in this tutorial uses the proto2 version of the protocol buffers language.

Also note that currently you can only create clients in PHP for gRPC services - you can find out how to create gRPC servers in our other tutorials, e.g. [Node.js](../node/route_guide).

This isn't a comprehensive guide to using gRPC in PHP: more reference documentation is coming soon.

- [Why use gRPC?](#why-grpc)
- [Example code and setup](#setup)
- [Try it out!](#try)
- [Defining the service](#proto)
- [Generating client code](#protoc)
- [Creating the client](#client)


<a name="why-grpc"></a>
## Why use gRPC?

With gRPC you can define your service once in a .proto file and implement clients and servers in any of gRPC's supported languages, which in turn can be run in environments ranging from servers inside Google to your own tablet - all the complexity of communication between different languages and environments is handled for you by gRPC. You also get all the advantages of working with protocol buffers, including efficient serialization, a simple IDL, and easy interface updating.


<a name="setup"></a>
## Example code and setup

The example code for our tutorial is in [examples/php/route_guide](.). To download the example, clone this repository by running the following command:
```shell
$ git clone https://github.com/grpc/grpc.git
```

Then change your current directory to `examples/php/route_guide`:
```shell
$ cd examples/php/route_guide
```

Our example is a simple route mapping application that lets clients get information about features on their route, create a summary of their route, and exchange route information such as traffic updates with the server and other clients.

You also should have the relevant tools installed to generate the client interface code (and a server in another language, for testing). You can obtain the latter by following [these setup instructions](https://github.com/grpc/homebrew-grpc).


<a name="try"></a>
## Try it out!

To try the sample app, we need a gRPC server running locally. Let's compile and run, for example, the Node.js server in this repository:

```shell
$ cd ../../node
$ npm install
$ cd route_guide
$ nodejs ./route_guide_server.js --db_path=route_guide_db.json
```

Run the PHP client (in a different terminal):

```shell
$ ./run_route_guide_client.sh
```

The next sections guide you step-by-step through how this proto service is defined, how to generate a client library from it, and how to create a client stub that uses that library.


<a name="proto"></a>
## Defining the service

First let's look at how the service we're using is defined. A gRPC *service* and its method *request* and *response* types using [protocol buffers](https://developers.google.com/protocol-buffers/docs/overview). You can see the complete .proto file for our example in [`route_guide.proto`](route_guide.proto).

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


<a name="protoc"></a>
## Generating client code

The PHP client stub implementation of the proto files can be generated by the [`protoc-gen-php`](https://github.com/datto/protobuf-php) tool. To install the tool:

```sh
$ cd examples/php
$ php composer.phar install
$ cd vendor/datto/protobuf-php
$ gem install rake ronn
$ rake pear:package version=1.0
$ sudo pear install Protobuf-1.0.tgz
```

To generate the client stub implementation .php file:

```sh
$ cd php/route_guide
$ protoc-gen-php -i . -o . ./route_guide.proto
```

A `route_guide.php` file will be generated in the `php/route_guide` directory. You do not need to modify the file.

To load the generated client stub file, simply `require` it in your PHP application:

```php
require dirname(__FILE__) . '/route_guide.php';
```

The file contains:
- All the protocol buffer code to populate, serialize, and retrieve our request and response message types.
- A class called `routeguide\RouteGuideClient` that lets clients call the methods defined in the `RouteGuide` service.


<a name="client"></a>
## Creating the client

In this section, we'll look at creating a PHP client for our `RouteGuide` service. You can see our complete example client code in [route_guide_client.php](route_guide_client.php).

### Constructing a client object

To call service methods, we first need to create a client object, an instance of the generated `RouteGuideClient` class. The constructor of the class expects the server address and port we want to connect to:

```php
$client = new routeguide\RouteGuideClient(new Grpc\BaseStub('localhost:50051', []));
```

### Calling service methods

Now let's look at how we call our service methods.

#### Simple RPC

Calling the simple RPC `GetFeature` is nearly as straightforward as calling a local asynchronous method.

```php
  $point = new routeguide\Point();
  $point->setLatitude(409146138);
  $point->setLongitude(-746188906);
  list($feature, $status) = $client->GetFeature($point)->wait();
```

As you can see, we create and populate a request object, i.e. an `routeguide\Point` object. Then, we call the method on the stub, passing it the request object. If there is no error, then we can read the response information from the server from our response object, i.e. an `routeguide\Feature` object.

```php
  print sprintf("Found %s \n  at %f, %f\n", $feature->getName(),
                $feature->getLocation()->getLatitude() / COORD_FACTOR,
                $feature->getLocation()->getLongitude() / COORD_FACTOR);
```

#### Streaming RPCs

Now let's look at our streaming methods. Here's where we call the server-side streaming method `ListFeatures`, which returns a stream of geographical `Feature`s:

```php
  $lo_point = new routeguide\Point();
  $hi_point = new routeguide\Point();

  $lo_point->setLatitude(400000000);
  $lo_point->setLongitude(-750000000);
  $hi_point->setLatitude(420000000);
  $hi_point->setLongitude(-730000000);

  $rectangle = new routeguide\Rectangle();
  $rectangle->setLo($lo_point);
  $rectangle->setHi($hi_point);

  $call = $client->ListFeatures($rectangle);
  // an iterator over the server streaming responses
  $features = $call->responses();
  foreach ($features as $feature) {
    // process each feature
  } // the loop will end when the server indicates there is no more responses to be sent.
```

The `$call->responses()` method call returns an iterator. When the server sends a response, a `$feature` object will be returned in the `foreach` loop, until the server indiciates that there will be no more responses to be sent.

The client-side streaming method `RecordRoute` is similar, except there we pass the method an iterator and get back a `routeguide\RouteSummary`.

```php
  $points_iter = function($db) {
    for ($i = 0; $i < $num_points; $i++) {
      $point = new routeguide\Point();
      $point->setLatitude($lat);
      $point->setLongitude($long);
      yield $point;
    }
  };
  // $points_iter is an iterator simulating client streaming
  list($route_summary, $status) =
    $client->RecordRoute($points_iter($db))->wait();
```

Finally, let's look at our bidirectional streaming RPC `routeChat()`. In this case, we just pass a context to the method and get back a `BidiStreamingCall` stream object, which we can use to both write and read messages.

```php
$call = $client->RouteChat();
```

To write messages from the client:

```php
  foreach ($notes as $n) {
    $route_note = new routerguide\RouteNote();
    $call->write($route_note);
  }
  $call->writesDone();
```

To read messages from the server:

```php
  while ($route_note_reply = $call->read()) {
    // process $route_note_reply
  }
```

Each side will always get the other's messages in the order they were written, both the client and server can read and write in any order — the streams operate completely independently.
