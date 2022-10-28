### Prerequisites

Whether you're using Windows, OS X, or Linux, you can follow this
example by using either an IDE and its build tools,
or by using the the .NET Core SDK command line tools.

First, make sure you have installed the
[gRPC C# prerequisites](https://github.com/grpc/grpc/blob/v1.46.x/src/csharp/README.md#prerequisites).
You will also need Git to download the sample code.

### Download the example

You'll need a local copy of the example code to work through this quick start.
Download the example code from our GitHub repository (the following command
clones the entire repository, but you just need the examples for this quick start
and other tutorials):

```sh
# Clone the repository to get the example code:
$ git clone -b v1.46.x --depth 1 --shallow-submodules https://github.com/grpc/grpc
$ cd grpc
```

This document will walk you through the "Hello World" example.
The projects and source files can be found in the `examples/csharp/Helloworld` directory.

The example in this walkthrough already adds the necessary
dependencies for you (`Grpc`, `Grpc.Tools` and `Google.Protobuf` NuGet packages).

### Build the example

#### Using Visual Studio (or Visual Studio for Mac)

* Open the solution `Greeter.sln` with Visual Studio
* Build the solution

#### Using .NET Core SDK from the command line

From the `examples/csharp/Helloworld` directory:

```sh
> dotnet build Greeter.sln
```

### Run a gRPC application

From the `examples/csharp/Helloworld` directory:

* Run the server:

  ```sh
  > cd GreeterServer
  > dotnet run
  ```

* From another terminal, run the client:

  ```sh
  > cd GreeterClient
  > dotnet run
  ```

Congratulations! You've just run a client-server application with gRPC.

### Update the gRPC service

Now let's look at how to update the application with an extra method on the
server for the client to call. Our gRPC service is defined using protocol
buffers; you can find out lots more about how to define a service in a `.proto`
file in [Basics tutorial](../RouteGuide/basics.md). For now all you need to know is that both the
server and the client "stub" have a `SayHello` RPC method that takes a
`HelloRequest` parameter from the client and returns a `HelloResponse` from the
server, and that this method is defined like this:


```proto
// The greeting service definition.
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

Let's update this so that the `Greeter` service has two methods. Edit
`examples/protos/helloworld.proto` and update it with a new `SayHelloAgain`
method, with the same request and response types:

```proto
// The greeting service definition.
service Greeter {
  // Sends a greeting
  rpc SayHello (HelloRequest) returns (HelloReply) {}
  // Sends another greeting
  rpc SayHelloAgain (HelloRequest) returns (HelloReply) {}
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

Remember to save the file!

### Generate gRPC code

Next we need to update the gRPC code used by our application to use the new service definition.

The `Grpc.Tools` NuGet package contains the protoc and protobuf C# plugin binaries needed
to generate the code. Starting from version 1.17 the package also integrates with
MSBuild to provide [automatic C# code generation](https://github.com/grpc/grpc/blob/v1.46.x/src/csharp/BUILD-INTEGRATION.md)
from `.proto` files.

This example project already depends on the `Grpc.Tools` NuGet package so just re-building the solution
is enough to regenerate the code from our modified `.proto` file.

You can rebuild just like we first built the original
example by running `dotnet build Greeter.sln` or by clicking "Build" in Visual Studio.

The build regenerates the following files
under the `Greeter/obj/Debug/TARGET_FRAMEWORK` directory:

* `Helloworld.cs` contains all the protocol buffer code to populate,
  serialize, and retrieve our request and response message types
* `HelloworldGrpc.cs` provides generated client and server classes,
  including:
    * an abstract class `Greeter.GreeterBase` to inherit from when defining
      Greeter service implementations
    * a class `Greeter.GreeterClient` that can be used to access remote Greeter
      instances

### Update and run the application

We now have new generated server and client code, but we still need to implement
and call the new method in the human-written parts of our example application.

#### Update the server

With the `Greeter.sln` open in your IDE, open `GreeterServer/Program.cs`.
Implement the new method by editing the GreeterImpl class like this:

```C#
class GreeterImpl : Greeter.GreeterBase
{
    // Server side handler of the SayHello RPC
    public override Task<HelloReply> SayHello(HelloRequest request, ServerCallContext context)
    {
        return Task.FromResult(new HelloReply { Message = "Hello " + request.Name });
    }

    // Server side handler for the SayHelloAgain RPC
    public override Task<HelloReply> SayHelloAgain(HelloRequest request, ServerCallContext context)
    {
        return Task.FromResult(new HelloReply { Message = "Hello again " + request.Name });
    }
}
```

#### Update the client

With the same `Greeter.sln` open in your IDE, open `GreeterClient/Program.cs`.
Call the new method like this:

```C#
public static void Main(string[] args)
{
    Channel channel = new Channel("127.0.0.1:50051", ChannelCredentials.Insecure);

    var client = new Greeter.GreeterClient(channel);
    String user = "you";

    var reply = client.SayHello(new HelloRequest { Name = user });
    Console.WriteLine("Greeting: " + reply.Message);

    var secondReply = client.SayHelloAgain(new HelloRequest { Name = user });
    Console.WriteLine("Greeting: " + secondReply.Message);

    channel.ShutdownAsync().Wait();
    Console.WriteLine("Press any key to exit...");
    Console.ReadKey();
}
```

#### Rebuild the modified example

Rebuild the newly modified example just like we first built the original
example by running `dotnet build Greeter.sln` or by clicking "Build" in Visual Studio.

#### Run!

Just like we did before, from the `examples/csharp/Helloworld` directory:

1. Run the server:

    ```sh
    > cd GreeterServer
    > dotnet run
    ```

2. From another terminal, run the client:

    ```sh
    > cd GreeterClient
    > dotnet run
    ```

