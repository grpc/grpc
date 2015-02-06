# Step-2: Write a service client.

This step uses the generated code to write a simple client to access the hello
service. The full client is in [GreetingsClient.java](src/main/java/ex/grpc/GreetingsClient.java).


## Configuring the service to connect to.

The client contains uses a Stub to contact the service. The internet address
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

## Obtaining a greeting

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

## Running from the command line

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

## Notes

- The client uses a blocking stub. This means that the RPC call waits for the
  server to respond, and will either return a response or raise an exception.

- gRPC Java has other kinds of stubs that make non-blocking calls to the
  server, where the response is returned asynchronously.  Usage of these stubs
  is a more advanced topic and will be described in later steps.
