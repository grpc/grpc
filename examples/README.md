
# Getting started

Welcome to the developer documentation for gRPC, a language-neutral,
platform-neutral remote procedure call (RPC) system developed at Google.

This document introduces you to gRPC with a quick overview and a simple
Hello World example. You'll find more tutorials and reference docs in this repository - more documentation is coming soon!

<a name="quickstart"></a>
## Quick start
You can find quick start guides for each language, including installation instructions, examples, and tutorials here:
* [C++](cpp)
* [Java](https://github.com/grpc/grpc-java/tree/master/examples)
* [Go](https://github.com/grpc/grpc-go/tree/master/examples)
* [Ruby](ruby)
* [Node.js](node)
* [Android Java](https://github.com/grpc/grpc-java/tree/master/examples/android)
* [Python](python/helloworld)
* [C#](csharp)
* [Objective-C](objective-c/helloworld)
* [PHP](php)

## What's in this repository?

The `examples` directory contains documentation, resources, and examples
for all gRPC users. You can find examples and instructions specific to your
favourite language in the relevant subdirectory.

You can find out about the gRPC source code repositories in
[`grpc`](https://github.com/grpc/grpc). Each repository provides instructions
for building the appropriate libraries for your language.


## What is gRPC?

In gRPC a *client* application can directly call
methods on a *server* application on a different machine as if it was a
local object, making it easier for you to create distributed applications and
services. As in many RPC systems, gRPC is based around the idea of defining
a *service*, specifying the methods that can be called remotely with their
parameters and return types. On the server side, the server implements this
interface and runs a gRPC server to handle client calls. On the client side,
the client has a *stub* that provides exactly the same methods as the server.

<!--TODO: diagram-->

gRPC clients and servers can run and talk to each other in a variety of
environments - from servers inside Google to your own desktop - and can
be written in any of gRPC's [supported languages](#quickstart). So, for
example, you can easily create a gRPC server in Java with clients in Go,
Python, or Ruby. In addition, the latest Google APIs will have gRPC versions
of their interfaces, letting you easily build Google functionality into
your applications.

<a name="protocolbuffers"></a>
### Working with protocol buffers

By default gRPC uses *protocol buffers*, Google’s
mature open source mechanism for serializing structured data (although it
can be used with other data formats such as JSON). As you'll
see in our example below, you define gRPC services using *proto files*,
with method parameters and return types specified as protocol buffer message
types. You
can find out lots more about protocol buffers in the [Protocol Buffers
documentation](https://developers.google.com/protocol-buffers/docs/overview).

#### Protocol buffer versions

While protocol buffers have been available for open source users for some
time, our examples use a new flavour of protocol buffers called proto3,
which has a slightly simplified syntax, some useful new features, and supports
lots more languages. This is currently available as an alpha release in
Java, C++, Java_nano (Android Java), Python, and Ruby from [the protocol buffers Github
repo](https://github.com/google/protobuf/releases), as well as a Go language
generator from [the golang/protobuf Github repo](https://github.com/golang/protobuf), with more languages in development. You can find out more in the [proto3 language guide](https://developers.google.com/protocol-buffers/docs/proto3), and see
the major differences from the current default version in the [release notes](https://github.com/google/protobuf/releases). More proto3 documentation is coming soon.

In general, while you *can* use proto2 (the current default protocol buffers version), we recommend that you use proto3 with gRPC as it lets you use the full range of gRPC-supported languages, as well as avoiding compatibility
issues with proto2 clients talking to proto3 servers and vice versa.

<a name="hello"></a>
## Hello gRPC!

Now that you know a bit more about gRPC, the easiest way to see how it
works is to look at a simple example. Our Hello World walks you through the
construction of a simple gRPC client-server application, showing you how to:

- Create a protocol buffers schema that defines a simple RPC service with
a single
Hello World method.
- Create a Java server that implements this interface.
- Create a Java client that accesses the Java server.
- Create a Go client that accesses
the same Java server.

The complete code for the example is available in the `examples`
directory. We use the Git versioning system for source code management:
however, you don't need to know anything about Git to follow along other
than how to install and run a few git commands.

This is an introductory example rather than a comprehensive tutorial, so
don't worry if you're not a Go or
Java developer - the concepts are similar for all languages, and you can
find more implementations of our Hello World example in other languages (and full tutorials where available) in
the [language-specific folders](#quickstart) in this repository. Complete tutorials and
reference documentation for all gRPC languages are coming soon.

<a name="setup"></a>
### Setup

This section explains how to set up your local machine to work with
the example code. If you just want to read the example, you can go straight
to the [next step](#servicedef).

#### Install Git

You can download and install Git from http://git-scm.com/download. Once
installed you should have access to the git command line tool. The main
commands that you will need to use are:

- git clone ... : clone a remote repository onto your local machine
- git checkout ... : check out a particular branch or a tagged version of
the code to hack on

#### Install gRPC

To build and install gRPC plugins and related tools:
- For Java, see the [Java quick start](https://github.com/grpc/grpc-java).
- For Go, see the [Go quick start](https://github.com/grpc/grpc-go).

#### Get the source code

The example code for our Java example lives in the `grpc-java`
GitHub repository. Clone this repository to your local machine by running the
following command:


```
git clone https://github.com/grpc/grpc-java.git
```

Change your current directory to grpc-java/examples

```
cd grpc-java/examples
```



<a name="servicedef"></a>
### Defining a service

The first step in creating our example is to define a *service*: an RPC
service specifies the methods that can be called remotely with their parameters
and return types. As you saw in the
[overview](#protocolbuffers) above, gRPC does this using [protocol
buffers](https://developers.google.com/protocol-buffers/docs/overview). We
use the protocol buffers interface definition language (IDL) to define our
service methods, and define the parameters and return
types as protocol buffer message types. Both the client and the
server use interface code generated from the service definition.

Here's our example service definition, defined using protocol buffers IDL in
[helloworld.proto](https://github.com/grpc/grpc-java/tree/master/examples/src/main/proto). The `Greeter`
service has one method, `SayHello`, that lets the server receive a single
`HelloRequest`
message from the remote client containing the user's name, then send back
a greeting in a single `HelloReply`. This is the simplest type of RPC you
can specify in gRPC - you can find out about other types in the tutorial for your chosen language.

```proto
syntax = "proto3";

option java_package = "io.grpc.examples";

package helloworld;

// The greeter service definition.
service Greeter {
  // Sends a greeting
  rpc SayHello (HelloRequest) returns (HelloReply) {}
}

// The request message containing the user's name.
message HelloRequest {
  string name = 1;
}

// The response message containing the greetings
message HelloReply {
  string message = 1;
}

```

<a name="generating"></a>
### Generating gRPC code

Once we've defined our service, we use the protocol buffer compiler
`protoc` to generate the special client and server code we need to create
our application - right now we're going to generate Java code, though you
can generate gRPC code in any gRPC-supported language (as you'll see later
in this example). The generated code contains both stub code for clients to
use and an abstract interface for servers to implement, both with the method
defined in our `Greeter` service.

(If you didn't install the gRPC plugins and protoc on your system and are just reading along with
the example, you can skip this step and move
onto the next one where we examine the generated code.)

For simplicity, we've provided a [Gradle build file](https://github.com/grpc/grpc-java/blob/master/examples/build.gradle) with our Java examples that runs `protoc` for you with the appropriate plugin, input, and output:

```shell
../gradlew build
```

This generates the following classes from our .proto, which contain all the generated code
we need to create our example:

- `Helloworld.java`, which
has all the protocol buffer code to populate, serialize, and retrieve our
`HelloRequest` and `HelloReply` message types
- `GreeterGrpc.java`, which contains (along with some other useful code):
    - an interface for `Greeter` servers to implement

    ```java
  public static interface Greeter {
      public void sayHello(io.grpc.examples.Helloworld.HelloRequest request,
          io.grpc.stub.StreamObserver<io.grpc.examples.Helloworld.HelloReply> responseObserver);
  }
    ```

    - _stub_ classes that clients can use to talk to a `Greeter` server. As you can see, they also implement the `Greeter` interface.

  ```java
  public static class GreeterStub extends
      io.grpc.stub.AbstractStub<GreeterStub, GreeterServiceDescriptor>
      implements Greeter {
   ...
  }
  ```

<a name="server"></a>
### Writing a server

Now let's write some code! First we'll create a server application to implement
our service. Note that we're not going to go into a lot of detail about how
to create a server in this section. More detailed information will be in the
tutorial for your chosen language: check if there's one available yet in the relevant [quick start](#quickstart).

Our server application has two classes:

- a main server class that hosts the service implementation and allows access over the
network: [HelloWorldServer.java](https://github.com/grpc/grpc-java/blob/master/examples/src/main/java/io/grpc/examples/helloworld/HelloWorldServer.java).


- a simple service implementation class [GreeterImpl.java](https://github.com/grpc/grpc-java/blob/master/examples/src/main/java/io/grpc/examples/helloworld/HelloWorldServer.java#L51).


#### Service implementation

[GreeterImpl.java](https://github.com/grpc/grpc-java/blob/master/examples/src/main/java/io/grpc/examples/helloworld/HelloWorldServer.java#L51)
actually implements our `Greeter` service's required behaviour.

As you can see, the class `GreeterImpl` implements the interface
`GreeterGrpc.Greeter` that we [generated](#generating) from our proto
[IDL](https://github.com/grpc/grpc-java/tree/master/examples/src/main/proto) by implementing the method `sayHello`:

```java
    @Override
    public void sayHello(HelloRequest req, StreamObserver<HelloReply> responseObserver) {
      HelloReply reply = HelloReply.newBuilder().setMessage("Hello " + req.getName()).build();
      responseObserver.onValue(reply);
      responseObserver.onCompleted();
    }
```
- `sayHello` takes two parameters:
    - `HelloRequest`: the request
    - `StreamObserver<HelloReply>`: a response observer, which is
    a special interface for the server to call with its response

To return our response to the client and complete the call:

1. We construct and populate a `HelloReply` response object with our exciting
message, as specified in our interface definition.
2. We return the `HelloReply` to the client and then specify that we've finished dealing with the RPC.


#### Server implementation

[HelloWorldServer.java](https://github.com/grpc/grpc-java/blob/master/examples/src/main/java/io/grpc/examples/helloworld/HelloWorldServer.java)
shows the other main feature required to provide a gRPC service; making the service
implementation available from the network.

```java
  /* The port on which the server should run */
  private int port = 50051;
  private ServerImpl server;

  private void start() throws Exception {
    server = NettyServerBuilder.forPort(port)
        .addService(GreeterGrpc.bindService(new GreeterImpl()))
        .build().start();
    logger.info("Server started, listening on " + port);
    Runtime.getRuntime().addShutdownHook(new Thread() {
      @Override
      public void run() {
        // Use stderr here since the logger may have been reset by its JVM shutdown hook.
        System.err.println("*** shutting down gRPC server since JVM is shutting down");
        HelloWorldServer.this.stop();
        System.err.println("*** server shut down");
      }
    });
  }

```

Here we create an appropriate gRPC server, binding the `Greeter` service
implementation that we created to a port. Then we start the server running: the server is now ready to receive
requests from `Greeter` service clients on our specified port. We'll cover
how all this works in a bit more detail in our language-specific documentation.

<a name="client"></a>
### Writing a client

Client-side gRPC is pretty simple. In this step, we'll use the generated code
to write a simple client that can access the `Greeter` server we created
in the [previous section](#server). You can see the complete client code in
[HelloWorldClient.java](https://github.com/grpc/grpc-java/blob/master/examples/src/main/java/io/grpc/examples/helloworld/HelloWorldClient.java).

Again, we're not going to go into much detail about how to implement a client;
we'll leave that for the tutorial.

#### Connecting to the service

First let's look at how we connect to the `Greeter` server. First we need
to create a gRPC channel, specifying the hostname and port of the server we
want to connect to. Then we use the channel to construct the stub instance.


```java
  private final ChannelImpl channel;
  private final GreeterGrpc.GreeterBlockingStub blockingStub;

  public HelloWorldClient(String host, int port) {
    channel =
        NettyChannelBuilder.forAddress(host, port).negotiationType(NegotiationType.PLAINTEXT)
            .build();
    blockingStub = GreeterGrpc.newBlockingStub(channel);
  }

```

In this case, we create a blocking stub. This means that the RPC call waits
for the server to respond, and will either return a response or raise an
exception. gRPC Java has other kinds of stubs that make non-blocking calls
to the server, where the response is returned asynchronously.

#### Calling an RPC

Now we can contact the service and obtain a greeting:

1. We construct and fill in a `HelloRequest` to send to the service.
2. We call the stub's `hello()` RPC with our request and get a `HelloReply`
back, from which we can get our greeting.


```java
    HelloRequest req = HelloRequest.newBuilder().setName(name).build();
    HelloReply reply = blockingStub.sayHello(req);

```

<a name="run"></a>
### Try it out!

Our [Gradle build file](https://github.com/grpc/grpc-java/blob/master/examples/build.gradle) simplifies building and running the examples.

You can build and run the server from the `grpc-java` root folder with:

```sh
$ ./gradlew :grpc-examples:helloWorldServer
```

and in another terminal window confirm that it receives a message.

```sh
$  ./gradlew :grpc-examples:helloWorldClient
```

### Adding another client

Finally, let's look at one of gRPC's most useful features - interoperability
between code in different languages. So far, we've just looked at Java code
generated from and implementing our `Greeter` service definition. However,
as you'll see if you look at the language-specific subdirectories
in this repository, we've also generated and implemented `Greeter`
in some of gRPC's other supported languages. Each service
and client uses interface code generated from the same proto
that we used for the Java example.

So, for example, if we visit the [`go` example
directory](https://github.com/grpc/grpc-go/tree/master/examples) and look at the
[`greeter_client`](https://github.com/grpc/grpc-go/blob/master/examples/greeter_client/main.go),
we can see that like the Java client, it connects to a `Greeter` service
at `localhost:50051` and uses a stub to call the `SayHello` method with a
`HelloRequest`:

```go
const (
	address = "localhost:50051"
	defaultName = "world"
)

func main() {
	// Set up a connection to the server.
	conn, err := grpc.Dial(address)
	if err != nil {
		log.Fatalf("did not connect: %v", err)
	}
	defer conn.Close()
	c := pb.NewGreeterClient(conn)

	// Contact the server and print out its response.
	name := defaultName
	if len(os.Args) > 1 {
		name = os.Args[1]
	}
	r, err := c.SayHello(context.Background(), &pb.HelloRequest{Name:
	name})
	if err != nil {
		log.Fatalf("could not greet: %v", err)
	}
	log.Printf("Greeting: %s", r.Message)
}
```


If we run the Java server from earlier in another terminal window, we can
run the Go client and connect to it just like the Java client, even though
it's written in a different language.

```
$ greeter_client
```
## Read more!

- You can find links to language-specific tutorials, examples, and other docs in each language's [quick start](#quickstart).
- [gRPC Authentication Support](doc/grpc-auth-support.md) introduces authentication support in gRPC with supported mechanisms and examples.
