#gRPC Basics: PHP

This tutorial provides a basic PHP programmer's introduction to working with gRPC. By walking through this example you'll learn how to:

- Define a service in a .proto file.
- Use the PHP gRPC API to write a simple client for your service.

It assumes that you have read the [Getting started](https://github.com/grpc/grpc-common) guide and are familiar with [protocol buffers] (https://developers.google.com/protocol-buffers/docs/overview).

This isn't a comprehensive guide to using gRPC in PHP: more reference documentation is coming soon.

## Why use gRPC?

Our example is a simple route mapping application that lets clients get information about features on their route, create a summary of their route, and exchange route information such as traffic updates with the server and other clients.

With gRPC we can define our service once in a .proto file and implement clients and servers in any of gRPC's supported languages, which in turn can be run in environments ranging from servers inside Google to your own tablet - all the complexity of communication between different languages and environments is handled for you by gRPC. We also get all the advantages of working with protocol buffers, including efficient serialization, a simple IDL, and easy interface updating.

## Example code and setup

The example code for our tutorial is in [grpc/grpc-common/php/route_guide](https://github.com/grpc/grpc-common/tree/master/php/route_guide). To download the example, clone the `grpc-common` repository by running the following command:
```shell
$ git clone https://github.com/grpc/grpc-common.git
```

Then change your current directory to `grpc-common/php/route_guide`:
```shell
$ cd grpc-common/php/route_guide
```

You also should have the relevant tools installed to generate the client interface code - if you don't already, follow the setup instructions in [the PHP quick start guide](https://github.com/grpc/grpc-common/tree/master/php).

Please note that currently we only support gRPC clients implemented in PHP. See the tutorials for our other supported languages (e.g. [Node.js](https://github.com/grpc/grpc-common/tree/master/node/route_guide)) to see how gRPC servers are implemented.


## Defining the service

Our first step (as you'll know from [Getting started](https://github.com/grpc/grpc-common)) is to define the gRPC *service* and the method *request* and *response* types using [protocol buffers] (https://developers.google.com/protocol-buffers/docs/overview). You can see the complete .proto file in [`grpc-common/protos/route_guide.proto`](https://github.com/grpc/grpc-common/blob/master/protos/route_guide.proto).

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


## Generating client stub from proto files

The PHP client stub implementation of the proto files can be generated by the [`protoc-gen-php`](https://github.com/datto/protobuf-php) tool.

```sh
$ cd grpc-common/php
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

Once you've done this, the client classes are in the `examples\` namespace (e.g. `examples\RouteGuideClient`).


<a name="client"></a>
## Creating the client

In this section, we'll look at creating a PHP client for our `RouteGuide` service. You can see our complete example client code in [grpc-common/php/route_guide/route_guide_client.php](https://github.com/grpc/grpc-common/blob/master/php/route_guide/route_guide_client.php). Again, please consult other languages (e.g. [Node.js](https://github.com/grpc/grpc-common/blob/master/node/route_guide/) to see how to start the route guide example server.

### Creating a stub

To call service methods, we first need to create a *stub*. To do this, we just need to call the RouteGuide stub constructor, specifying the server address and port.

```php
$client = new examples\RouteGuideClient(new Grpc\BaseStub('localhost:50051', []));
```

### Calling service methods

Now let's look at how we call our service methods.

#### Simple RPC

Calling the simple RPC `GetFeature` is nearly as straightforward as calling a local asynchronous method.

```php
$point = new examples\Point();
$point->setLatitude(409146138);
$point->setLongitude(-746188906);
list($feature, $status) = $client->GetFeature($point)->wait();
```

As you can see, we create and populate a request object, i.e. an `examples\Point` object. Then, we call the method on the stub, passing it the request object. If there is no error, then we can read the response information from the server from our response object, i.e. an `examples\Feature` object.

```php
  print sprintf("Found %s \n  at %f, %f\n", $feature->getName(),
                $feature->getLocation()->getLatitude() / COORD_FACTOR,
                $feature->getLocation()->getLongitude() / COORD_FACTOR);
```

#### Streaming RPCs

Now let's look at our streaming methods. Here's where we call the server-side streaming method `ListFeatures`, which returns a stream of geographical `Feature`s:

```php
  $lo_point = new examples\Point();
  $hi_point = new examples\Point();

  $lo_point->setLatitude(400000000);
  $lo_point->setLongitude(-750000000);
  $hi_point->setLatitude(420000000);
  $hi_point->setLongitude(-730000000);

  $rectangle = new examples\Rectangle();
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

The client-side streaming method `RecordRoute` is similar, except there we pass the method an iterator and get back a `examples\RouteSummary`.

```php
  $points_iter = function($db) {
    for ($i = 0; $i < $num_points; $i++) {
      $point = new examples\Point();
      $point->setLatitude($lat);
      $point->setLongitude($long);
      yield $point;
    }
  };
  // $points_iter is an iterator simulating client streaming
  list($route_summary, $status) =
    $client->RecordRoute($points_iter($db))->wait();
```

Finally, let's look at our bidirectional streaming RPC `routeChat()`. In this case, we just pass a context to the method and get back a `Duplex` stream object, which we can use to both write and read messages.

```php
$call = $client->RouteChat();
```

To write messages from the client:

```php
  foreach ($notes as $n) {
    $route_note = new examples\RouteNote();
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

## Try it out!

Run the client (in a different terminal):
```shell
$ ./run_route_guide_client.sh
```
