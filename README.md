# Getting started

Welcome to the developer documentation for gRPC, a language-neutral,
platform-neutral remote procedure call (RPC) system developed at Google.

This document introduces you to gRPC with a quick overview and a simple
Hello World example. More documentation is coming soon!

## What is gRPC?

In gRPC a *client* application can directly call
methods on a *server* application on a different machine as if it was a
local object, making it easier for you to create distributed applications and
services. As in many RPC systems, gRPC is based around the idea of defining
a *service*, specifying the methods that can be called remotely with their
parameters and return types. On the server side, the server implements this
interface and runs a gRPC server to handle client calls. On the client side,
the client has a *stub* that provides exactly the same methods as the server.

##TODO: diagram?

gRPC clients and servers can run and talk to each other in a variety of
environments - from servers inside Google to your own desktop - and can
be written in any of gRPC's [supported languages](link to list). So, for
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
[languages] from [wherever it's going], with more languages in development.

In general, we recommend that you use proto3 with gRPC as it lets you use the
full range of gRPC-supported languages, as well as avoiding compatibility
issues with proto2 clients talking to proto3 servers and vice versa. You
can find out more about these potential issues in [where should we put this
info? It's important but not really part of an overview]. If you need to
continue using proto2 for Java, C++, or Python but want
to try gRPC, you can see an example using a proto2 gRPC client and server
[wherever we put it].


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
- Create a [probably need a different language now] client that accesses
the same Java server.
- Update the service with more advanced features like RPC streaming.

The complete code for the example is available in the `grpc-common` GitHub
repository. You can
work along with the example and hack on the code in the comfort of your own
computer, giving you hands-on practice of really writing
gRPC code. We use the Git versioning system for source code management:
however, you don't need to know anything about Git to follow along other
than how to install and run a few git commands.

This is an introductory example rather than a comprehensive tutorial, so
don't worry if you're not a Go or
Java developer - the concepts introduced here are similar for all languages,
and complete tutorials and reference documentation for all gRPC
languages are coming soon.

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

#### Get the source code

The example code for this and our other examples lives in the `grpc-common`
GitHub repository. Clone this repository to your local machine by running the
following command:


```
git clone https://github.com/google/grpc-common.git
```

Change your current directory to grpc-common/java

```
cd grpc-common/java
```

#### Install Java 8

Java gRPC is designed to work with both Java 7 and Java 8 - our example uses
Java 8. See
[Install Java
8](http://docs.oracle.com/javase/8/docs/technotes/guides/install/install_overview.html)
for instructions if you need to install Java 8.

#### Install Maven

To simplify building and managing gRPC's dependencies, the Java client
and server are structured as a standard
[Maven](http://maven.apache.org/guides/getting-started/)
project. See [Install Maven](http://maven.apache.org/users/index.html)
for instructions.


#### Install Go 1.4

Go gRPC requires Go 1.4, the latest version of Go.  See
[Install Go](https://golang.org/doc/install) for instructions.

#### (optional) Install protoc

gRPC uses the latest version of the [protocol
buffer](https://developers.google.com/protocol-buffers/docs/overview)
compiler, protoc.

Having protoc installed isn't strictly necessary to follow along with this
example, as all the
generated code is checked into the Git repository. However, if you want
to experiment
with generating the code yourself, download and install protoc from its
[Git repo](https://github.com/google/protobuf)

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
[helloworld.proto](java/src/main/proto/helloworld.proto). The `Greeting`
service has one method, `hello`, that lets the server receive a single
`HelloRequest`
message from the remote client containing the user's name, then send back
a greeting in a single `HelloReply`. This is the simplest type of RPC you
can specify in gRPC - we'll look at some other types later in this document.

```
syntax = "proto3";

option java_package = "ex.grpc";

package helloworld;

// The greeting service definition.
service Greeter {
  // Sends a greeting
  rpc sayHello (HelloRequest) returns (HelloReply) {}
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
defined in our `Greeting` service.

(If you didn't install `protoc` on your system and are working along with
the example, you can skip this step and move
onto the next one where we examine the generated code.)

As this is our first time using gRPC, we need to build the protobuf plugin
that generates our RPC
classes. By default `protoc` just generates code for reading and writing
protocol buffers, so you need to use plugins to add additional features
to generated code. As we're creating Java code, we use the gRPC Java plugin.

To build the plugin:

```sh
$ pushd external/grpc_java
$ make java_plugin
$ popd
```

To use it to generate the code:

```sh
$ mkdir -p src/main/java
$ protoc -I . helloworld.proto
--plugin=protoc-gen-grpc=external/grpc_java/bins/opt/java_plugin \
                               --grpc_out=src/main/java \
                               --java_out=src/main/java
```

This generates the following classes, which contain all the generated code
we need to create our example:

- [`Helloworld.java`](java/src/main/java/ex/grpc/Helloworld.java), which
has all the protocol buffer code to populate, serialize, and retrieve our
`HelloRequest` and `HelloReply` message types
- [`GreeterGrpc.java`](java/src/main/java/ex/grpc/GreeterGrpc.java),
which contains (along with some other useful code):
    - an interface for `Greeter` servers to implement

    ```java
  public static interface Greeter {

    public void sayHello(ex.grpc.Helloworld.HelloRequest request,
        com.google.net.stubby.stub.StreamObserver<ex.grpc.Helloworld.HelloReply>
        responseObserver);
  }
    ```

    - _stub_ classes that clients can use to talk to a `Greeter` server. As you can see, they also implement the `Greeter` interface.

  ```java
public static class GreeterStub extends
      com.google.net.stubby.stub.AbstractStub<GreeterStub,
      GreeterServiceDescriptor>
      implements Greeter {
   ...
  }
  ```

<a name="server"></a>
### Writing a server

Now let's write some code! First we'll create a server application to implement
our service. Note that we're not going to go into a lot of detail about how
to create a server in this section. More detailed information will be in the
tutorial for your chosen language (coming soon).

Our server application has two classes:

- a simple service implementation
[GreeterImpl.java](java/src/main/java/ex/grpc/GreeterImpl.java).

- a server that hosts the service implementation and allows access over the
network: [GreeterServer.java](java/src/main/java/ex/grpc/GreeterServer.java).

#### Service implementation

[GreeterImpl.java](java/src/main/java/ex/grpc/GreeterImpl.java)
actually implements our GreetingService's required behaviour.

As you can see, the class `GreeterImpl` implements the interface
`GreeterGrpc.Greeter` that we [generated](#generating) from our proto
[IDL](java/src/main/proto/helloworld.proto) by implementing the method `hello`:

```java
  public void hello(Helloworld.HelloRequest req,
      StreamObserver<Helloworld.HelloReply> responseObserver) {
    Helloworld.HelloReply reply =
    Helloworld.HelloReply.newBuilder().setMessage(
        "Hello " + req.getName()).build();
    responseObserver.onValue(reply);
    responseObserver.onCompleted();
  }
```
- `hello` takes two parameters:
    -`Helloworld.HelloRequest`: the request
    -`StreamObserver<Helloworld.HelloReply>`: a response observer, which is
    a special interface for the server to call with its response

To return our response to the client and complete the call:

1. We construct and populate a `HelloReply` response object with our exciting
message, as specified in our interface definition.
2. We use the`responseObserver` to return the `HelloReply` to the client
and then specify that we've finished dealing with the RPC


#### Server implementation

[GreeterServer.java](java/src/main/java/ex/grpc/GreeterServer.java)
shows the other main feature required to provide a gRPC service; making the service
implementation available from the network.

```java
  private ServerImpl server;
  ...
  private void start() throws Exception {
    server = NettyServerBuilder.forPort(port)
             .addService(GreeterGrpc.bindService(new GreeterImpl()))
             .build();
    server.startAsync();
    server.awaitRunning(5, TimeUnit.SECONDS);
  }

```

Here we create an appropriate gRPC server, binding the `GreeterService`
implementation that we created to a port. Then we start the server running: the server is now ready to receive
requests from `Greeter` service clients on our specified port. We'll cover
how all this works in a bit more detail in our language-specific documentation.

#### Build it

Once we've implemented everything, we use Maven to build the server:

```
$ mvn package
```

We'll look at using a client to access the server in the next section.

<a name="client"></a>
### Writing a client

Client-side gRPC is pretty simple. In this step, we'll use the generated code
to write a simple client that can access the `Greeter` server we created
in the [previous section](#server). You can see the complete client code in
[GreeterClient.java](java/src/main/java/ex/grpc/GreeterClient.java).

Again, we're not going to go into much detail about how to implement a client;
we'll leave that for the tutorial.

#### Connecting to the service

First let's look at how we connect to the `Greetings` server. First we need
to create a gRPC channel, specifying the hostname and port of the server we
want to connect to. Then we use the channel to construct the stub instance.


```java
  private final ChannelImpl channel;
  private final GreeterGrpc.GreeterBlockingStub blockingStub;

  public HelloClient(String host, int port) {
    channel = NettyChannelBuilder.forAddress(host, port)
              .negotiationType(NegotiationType.PLAINTEXT)
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
back,
from which we can get our greeting.


```java
  public void greet(String name) {
    logger.debug("Will try to greet " + name + " ...");
    try {
      Helloworld.HelloRequest request =
      Helloworld.HelloRequest.newBuilder().setName(name).build();
      Helloworld.HelloReply reply = blockingStub.sayHello(request);
      logger.info("Greeting: " + reply.getMessage());
    } catch (RuntimeException e) {
      logger.log(Level.WARNING, "RPC failed", e);
      return;
    }
  }

```

#### Build the client

This is the same as building the server: our client and server are part of
the same maven package so the same command builds both.

```
$ mvn package
```

<a name="run"></a>
### Try it out!

We've added simple shell scripts to simplifying running the examples. Now
that they are built, you can run the server with:

```sh
$ ./run_greeter_server.sh
```

and in another terminal window confirm that it receives a message.

```sh
$ ./run_greeter_client.sh
```

### Adding another client


Finally, let's look at one of gRPC's most useful features - interoperability
between code in different languages. So far, we've just generated Java code
from our `Greeter` service definition....

###TODO: Section on Go client for same server
