# Getting started

## TODO: move this to the tutorial sub-folder

Welcome to the developer documentation for gRPC, a language-neutral,
platform-neutral remote procedure call (RPC) system developed at Google that
helps you build connected systems.

This document introduces you to gRPC with a quick overview and a simple
Hello World example. More documentation is coming soon!

## What is gRPC?

## TODO: basic conceptual intro (anything more in-depth will go in gRPC Concepts doc)

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

The complete code for the example is available in [wherever we put it]. You can
work along with the example and hack on the code in the comfort of your own
computer, giving you hands-on practice of really writing
gRPC code. We use the Git versioning system for source code management:
however, you don't need to know anything about Git to follow along other
than how to install and run a few git commands.

This is an introductory example rather than a comprehensive tutorial, so
don't worry if you're not a Go or
Java developer - complete tutorials and reference documentation for all gRPC
languages are coming soon.

### Setup

The rest of this page explains how to set up your local machine to work with
the example code.
If you just want to read the example, you can go straight to the next step:
[Step - 0](Step_0.md)

#### Install Git

You can download and install Git from http://git-scm.com/download. Once
installed you should have access to the git command line tool. The main
commands that you will need to use are:

- git clone ... : clone a remote repository onto your local machine
- git checkout ... : check out a particular branch or a tagged version of
the code to hack on

#### Download grpc-helloworld

Clone the grpc-helloword repository located at GitHub by running the
following command:

```
git clone https://github.com/google/grpc-helloworld.git
```

Change your current directory to grpc-helloworld

```
cd grpc-helloworld
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
[helloworld.proto](src/main/proto/helloworld.proto). The `Greeting` service
has one method, `hello`, that lets the server receive a single `HelloRequest`
message from the remote client containing the user's name, then send back
a greeting in a `HelloReply`.

```
syntax = "proto3";

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

As this is our first time using gRPC, we need to build the protobuf plugin that generates our RPC
classes. By default `protoc` just generates code for reading and writing
protocol buffers, so you need to use plugins to add additional features
to generated code. As we're creating Java code, we use the gRPC Java plugin.

To build the plugin:

```
$ pushd external/grpc_java
$ make java_plugin
$ popd
```

To use it to generate the code:

```
$ mkdir -p src/main/java
$ protoc -I . helloworld.proto
--plugin=protoc-gen-grpc=external/grpc_java/bins/opt/java_plugin \
                               --grpc_out=src/main/java \
                               --java_out=src/main/java
```

This generates the following Java classes

### Writing a client

Now let's write some code! Client-side gRPC is pretty simple, so we'll start there - we'll look at how to implement a gRPC server later. In this step, we'll use the generated code to write a simple client that can access the `Greetings` service. You can see the complete client code in [GreetingsClient.java](src/main/java/ex/grpc/GreetingsClient.java).

Note that we're not going to go into much detail about how to implement a client - we'll leave that for the tutorial.

#### Connecting to the service

. The internet address
is configured in the client constructor. gRPC Channel is the abstraction over
transport handling; its constructor accepts the host name and port of the
service. The channel in turn is used to construct the Stub.


```
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


```
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

```
    /* Access a service running on the local machine on port 50051 */
    HelloClient client = new HelloClient("localhost", 50051);
    String user = "world";
    if (args.length > 1) {
      user = args[1];
    }
    client.greet(user);

```

It can be built as follows.

```
$ mvn package
```

It can also be run, but doing so now would end up a with a failure as there is
no server available yet.  The [next step](Step_3.md), describes how to
implement, build and run a server that supports the service description.

#### Notes

- The client uses a blocking stub. This means that the RPC call waits for the
  server to respond, and will either return a response or raise an exception.

- gRPC Java has other kinds of stubs that make non-blocking calls to the
  server, where the response is returned asynchronously.  Usage of these stubs
  is a more advanced topic and will be described in later steps.


We haven't looked at implementing a server yet, but 


