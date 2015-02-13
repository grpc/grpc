# Getting started

## TODO: move this to the tutorial sub-folder

Welcome to the developer documentation for gRPC, a language-neutral,
platform-neutral remote procedure call (RPC) system developed at Google.

This document introduces you to gRPC with a quick overview and a simple
Hello World example. More documentation is coming soon!

## What is gRPC?

## TODO: basic conceptual intro (anything more in-depth will go in gRPC Concepts doc)

<a name="hello"></a>
## Hello gRPC!

Now that you know a bit more about gRPC, the easiest way to see how it
works is to look at a simple example. Our Hello World walks you through the
construction of a simple gRPC client-server application, showing you how to:

- Create a protobuf schema that defines a simple RPC service with a single
Hello World method.
- Create a Java server that implements the schema interface.
- Create a Java client that accesses the Java server.
- Create a Go client that accesses the same Java server.
- Update the service with more advanced features like RPC streaming.

The complete code for the example is available in the `grpc-common` GitHub repository. You can
work along with the example and hack on the code in the comfort of your own
computer, giving you hands-on practice of really writing
gRPC code. We use the Git versioning system for source code management:
however, you don't need to know anything about Git to follow along other
than how to install and run a few git commands.

This is an introductory example rather than a comprehensive tutorial, so
don't worry if you're not a Go or
Java developer - complete tutorials and reference documentation for all gRPC
languages are coming soon.

<a name="setup"></a>
### Setup

The rest of this page explains how to set up your local machine to work with
the example code.
If you just want to read the example, you can go straight to the [next step](#servicedef).

#### Install Git

You can download and install Git from http://git-scm.com/download. Once
installed you should have access to the git command line tool. The main
commands that you will need to use are:

- git clone ... : clone a remote repository onto your local machine
- git checkout ... : check out a particular branch or a tagged version of
the code to hack on

#### Get the source code

The example code for this and our other examples lives in the `grpc-common` GitHub repository. Clone this repository to your local machine by running the
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
and return types. In gRPC, we use the protocol buffers interface definition
language (IDL) to define our service methods, and the parameters and return
types are defined as protocol buffer message types. Both the client and the
server use interface code generated from the service definition. If you're not
familiar with protocol buffers, you can find out more in the [Protocol Buffers
Developer Guide](https://developers.google.com/protocol-buffers/docs/overview).

Here's our example service definition, defined using protocol buffers IDL in
[helloworld.proto](java/src/main/proto/helloworld.proto). The `Greeting` service
has one method, `hello`, that lets the server receive a single `HelloRequest`
message from the remote client containing the user's name, then send back
a greeting in a single `HelloReply`. This is the simplest type of RPC you
can specify in gRPC - we'll look at some other types later in this document.

```
syntax = "proto3";

option java_package = "ex.grpc";

package helloworld;

// The request message containing the user's name.
message HelloRequest {
  optional string name = 1;
}

// The response message containing the greetings
message HelloReply {
  optional string message = 1;
}

// The greeting service definition.
service Greeting {

  // Sends a greeting
  rpc hello (HelloRequest) returns (HelloReply) {
  }
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
defined in our `Greeting` service. A stub is code that initiates contact
with a gRPC service running remotely via the internet. [can probably define
this up in "what is gRPC"?]

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
- [`GreetingsGrpc.java`](java/src/main/java/ex/grpc/GreetingsGrpc.java),
which contains (along with some other useful code):
    - an interface for `Greetings` servers to implement

    ```java
  public static interface Greetings {

    public void hello(ex.grpc.Helloworld.HelloRequest request,
        com.google.net.stubby.stub.StreamObserver<ex.grpc.Helloworld.HelloReply>
        responseObserver);
  }
    ```

    - _stub_ classes that clients can use to talk to a `Greetings` server.

  ```java
public static class GreetingsStub extends
      com.google.net.stubby.stub.AbstractStub<GreetingsStub,
      GreetingsServiceDescriptor>
      implements Greetings {
   ...
  }
  ```

<a name="server"></a>
### Writing a server

Now let's write some code! First we'll create a server application to implement
our service. Note that we're not going to go into a lot of detail about how
to create a server in this section More detailed information will be in the
tutorial for your chosen language (coming soon).

Our server application has two classes:

- a simple service implementation
[GreetingsImpl.java](java/src/main/java/ex/grpc/GreetingsImpl.java).

- a server that hosts the service implementation and allows access over the
network: [GreetingsServer.java](src/main/java/ex/grpc/GreetingsServer.java).

#### Service implementation

[GreetingsImpl.java](java/src/main/java/ex/grpc/GreetingsImpl.java)
actually implements our GreetingService's required behaviour.

As you can see, the class `GreetingsImpl` implements the interface
`GreetingsGrpc.Greetings` that we [generated](#generating) from our proto
[IDL](src/main/proto/helloworld.proto) by implementing the method `hello`:

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
- `hello's` signature is typesafe:
   `hello(Helloworld.HelloRequest req, StreamObserver<Helloworld.HelloReply>
   responseObserver)`
- `hello` takes two parameters:
    -`Helloworld.HelloRequest`: the request
    -`StreamObserver<Helloworld.HelloReply>`: a response observer, which is
    a special interface for the server to call with its response

To return our response to the client and complete the call:

1. We construct and populate a `HelloReply` response object with our exciting
message, as specified in our interface definition.
2. We call `responseObserver.onValue()` with the `HelloReply` that we want to send back to the client.
3. Finally, we call `responseObserver.onCompleted()` to indicate that we're
finished dealing with this RPC.


#### Server implementation

[GreetingsServer.java](src/main/java/ex/grpc/GreetingsServer.java) shows the
other main feature required to provde the gRPC service; making the service
implementation available from the network.

```java
  private void start() throws Exception {
    server = NettyServerBuilder.forPort(port)
             .addService(GreetingsGrpc.bindService(new GreetingsImpl()))
             .build();
    server.startAsync();
    server.awaitRunning(5, TimeUnit.SECONDS);
  }

```

- it provides a class `GreetingsServer` that holds a `ServerImpl` that will run the server
- in the `start` method, `GreetingServer` binds the `GreetingsService` implementation to a port and begins running it
- there is also a `stop` method that takes care of shutting down the service and cleaning up when the program exits

#### Build it

Once we've implemented everything, we use Maven to build the server:

```
$ mvn package
```

We'll look at using a client to access the server in the next section.

<a name="client"></a>
### Writing a client

Client-side gRPC is pretty simple. In this step, we'll use the generated code to write a simple client that can access the `Greetings` server we created in the previous section. You can see the complete client code in [GreetingsClient.java](src/main/java/ex/grpc/GreetingsClient.java).

Again, we're not going to go into much detail about how to implement a client - we'll leave that for the tutorial.

#### Connecting to the service

. The internet address
is configured in the client constructor. gRPC Channel is the abstraction over
transport handling; its constructor accepts the host name and port of the
service. The channel in turn is used to construct the Stub.


```java
  private final ChannelImpl channel;
  private final GreetingGrpc.GreetingBlockingStub blockingStub;

  public HelloClient(String host, int port) {
    channel = NettyChannelBuilder.forAddress(host, port)
              .negotiationType(NegotiationType.PLAINTEXT)
              .build();
    blockingStub = GreetingGrpc.newBlockingStub(channel);
  }

```

#### Obtaining a greeting

The greet method uses the stub to contact the service and obtain a greeting.
It:
- constructs a request
- obtains a reply from the stub
- prints out the greeting


```java
  public void greet(String name) {
    logger.debug("Will try to greet " + name + " ...");
    try {
      Helloworld.HelloRequest request = Helloworld.HelloRequest.newBuilder().setName(name).build();
      Helloworld.HelloReply reply = blockingStub.hello(request);
      logger.info("Greeting: " + reply.getMessage());
    } catch (RuntimeException e) {
      logger.log(Level.WARNING, "RPC failed", e);
      return;
    }
  }

```

#### Running from the command line

The main method puts together the example so that it can be run from a command
line.

```java
    /* Access a service running on the local machine on port 50051 */
    HelloClient client = new HelloClient("localhost", 50051);
    String user = "world";
    if (args.length > 1) {
      user = args[1];
    }
    client.greet(user);

```

#### Build the client

This is the same as before: our client and server are part of the same maven
package so the same command builds both.

```
$ mvn package
```

#### Notes

- The client uses a blocking stub. This means that the RPC call waits for the
  server to respond, and will either return a response or raise an exception.

- gRPC Java has other kinds of stubs that make non-blocking calls to the
  server, where the response is returned asynchronously.  Usage of these stubs
  is a more advanced topic and will be described in later steps.

<a name="run"></a>
### Try it out!

We've added simple shell scripts to simplifying running the examples. Now
that they are built, you can run the server with:

```sh
$ ./run_greetings_server.sh
```

and in another terminal window confirm that it receives a message.

```sh
$ ./run_greetings_client.sh
```





