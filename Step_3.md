# Step-3: Implement a server.

This step extends the generated server skeleton code to write a simple server
that provides the hello service. This introduces two new classes:

- a service implementation [GreetingsImpl.java](src/main/java/ex/grpc/GreetingsImpl.java).

- a server that hosts the service implementation and allows access over the network: [GreetingsServer.java](src/main/java/ex/grpc/GreetingsServer.java).

## Service implementation

[GreetingsImpl.java](src/main/java/ex/grpc/GreetingsImpl.java)
implements the behaviour we require of our GreetingService. There are a
number of important features of gRPC being used here:

```
  public void hello(Helloworld.HelloRequest req,
      StreamObserver<Helloworld.HelloReply> responseObserver) {
    Helloworld.HelloReply reply = Helloworld.HelloReply.newBuilder().setMessage(
        "Hello " + req.getName()).build();
    responseObserver.onValue(reply);
    responseObserver.onCompleted();
  }
```

- it provides a class `GreetingsImpl` that implements a generated interface `GreetingsGrpc.Greetings`
- `GreetingsGrpc.Greetings` declares the method `hello` that was declared in the proto [IDL](src/main/proto/helloworld.proto)
- `hello's` signature is typesafe:
   hello(Helloworld.HelloRequest req, StreamObserver<Helloworld.HelloReply> responseObserver)
- `hello` takes two parameters:
  `Helloworld.HelloRequest`: the request
  `StreamObserver<Helloworld.HelloReply>`: a response observer, an interface to be called with the response value
- to complete the call
  - the return value is constructed
  - the responseObserver.onValue() is called with the response
  - responseObserver.onCompleted() is called to indicate that no more work will done on the RPC.


## Server implementation

[GreetingsServer.java](src/main/java/ex/grpc/GreetingsServer.java) shows the
other main feature required to provde the gRPC service; how to allow a service
implementation to be accessed from the network.

```
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

## Build it

This is the same as before: our client and server are part of the same maven
package so the same command builds both.

```
$ mvn package
```

## Try them out

We've added simple shell scripts to simplifying running the examples. Now
that they are built, you can run the server with:

```
$ ./run_greetings_server.sh
```

and in another terminal window confirm that it receives a message.

```
$ ./run_greetings_client.sh
```
