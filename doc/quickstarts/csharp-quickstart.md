---
layout: docs
title: C# Quickstart
---

<h1 class="page-header">C# Quickstart</h1>

<p class="lead">This guide gets you started with gRPC in C# with a simple working example.</p>

<div id="toc"></div>

## Before you begin

### Prerequisites

* Windows: .NET Framework 4.5+, Visual Studio 2013 or 2015. You may also need the Nuget executable 
* Linux: Mono 4+, MonoDevelop 5.9+ (with NuGet add-in installed)
* Mac OS X: Xamarin Studio 5.9+

### How to use Grpc

The example solution in this walkthrough already adds the necessary
dependencies on Grpc, but this explains how to use Grpc in your projects.

**Windows**

- Open Visual Studio and start a new project/solution. 
- Add NuGet package `Grpc` as a dependency (Project options -> Manage NuGet Packages).
  That will also pull all the transitive dependencies (including the gRPC native library that
  gRPC C# is using internally).
  
**Linux (Debian)**

- Open MonoDevelop and start a new project/solution.

- Add NuGet package `Grpc` as a dependency (Project -> Add NuGet packages).
  That will also pull all the transitive dependencies (including the gRPC native library that
  gRPC C# is using internally).

**Mac OS X**

- Open Xamarin Studio and start a new project/solution.

- Add NuGet package `Grpc` as a dependency (Project -> Add NuGet packages).
  That will also pull all the transitive dependencies (including the gRPC native library that
  gRPC C# is using internally).
  
Example projects depend on the Grpc, Grpc.Tools and Google.Protobuf NuGet packages, which have been already added to the project for you.

## Download the example

You'll need a local copy of the example code to work through this quickstart. 
Download the example code from our Github repository (the following command clones the entire repository, but you just need the examples for this quickstart and other tutorials):

```sh
$ # Clone the repository to get the example code:
$ git clone -b $(curl -L http://grpc.io/release) https://github.com/grpc/grpc 
```

All of the files in this quickstart are in the directory `examples/csharp/helloworld`.

## Build the example

* Open solution `Greeter.sln` with Visual Studio, Monodevelop (on Linux) or Xamarin Studio (on Mac OS X)
* Build the solution (this will automatically download NuGet dependencies)
  
## Run a gRPC application

From the `examples/csharp/helloworld` directory:

1. Run the server

```
> cd GreeterServer/bin/Debug
> GreeterServer.exe
```

2. In another terminal, run the client

```
> cd GreeterClient/bin/Debug
> GreeterClient.exe
```

Note to run the above executables with "mono" if building on Xamarin Studio for Mac OS X.

Congratulations! You've just run a client-server application with gRPC.

## Update a gRPC service

Now let's look at how to update the application with an extra method on the
server for the client to call. Our gRPC service is defined using protocol
buffers; you can find out lots more about how to define a service in a `.proto`
file in [What is gRPC?]() and [gRPC Basics: C#][]. For now all you need
to know is that both the server and the client "stub" have a `SayHello` RPC
method that takes a `HelloRequest` parameter from the client and returns a
`HelloResponse` from the server, and that this method is defined like this:


```
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

Let's update this so that the `Greeter` service has two methods. Edit `examples/proto/helloworld.proto` and update it with a new `SayHelloAgain` method, with the same request and response types:

```
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

(Don't forget to save the file!)

## Generate gRPC code

Next we need to update the gRPC code used by our application to use the new service definition. 
The Grpc.Tools NuGet package contains the protoc and protobuf C# plugin binaries you will need to generate the code. This example project already depends on the nuget package `Grpc.Tools.0.15.0`, so it should be included in the `examples/csharp/helloworld/packages` when the `Greeter.sln` solution is built from your IDE. 

Note that you may have to change the `<platform>_<architecture>` directory names (e.g. windows_x86, linux_x64) in the commands below based on your environment.

Note that you may also have to change the permissions of the protoc and protobuf binaries in the `Grpc.Tools` package under `examples/csharp/helloworld/packages` to executable, in order to run the commands below.

From the `examples/csharp/helloworld` directory:

**Windows**

```
$ packages/Grpc.Tools.0.15.0/tools/windows_x86/protoc -I../../protos --csharp_out Greeter --grpc_out Greeter ../../protos/helloworld.proto --plugin=protoc-gen-grpc=packages/Grpc.Tools.0.15.0/tools/windows_x86/grpc_csharp_plugin
```

**Linux (or Mac OS X by using macosx_x64 directory).**

```
$ packages/Grpc.Tools.0.15.0/tools/linux_x64/protoc -I../../protos --csharp_out Greeter --grpc_out Greeter ../../protos/helloworld.proto --plugin=protoc-gen-grpc=packages/Grpc.Tools.0.15.0/tools/linux_x64/grpc_csharp_plugin
```

Running the appropriate command for your OS regenerates the following files in the directory:
 
* Greeter/Helloworld.cs contains all the protocol buffer code to populate, serialize, and retrieve our request and response message types
* Greeter/HelloworldGrpc.cs provides generated client and server classes, including:
    * an abstract class Greeter.GreeterBase to inherit from when defining Greeter service implementations
    * a class Greeter.GreeterClient that can be used to access remote Greeter instances
    
## Update and run the application

We now have new generated server and client code, but we still need to implement and call the new method in the human-written parts of our example application.

### Update the server

With the `Greeter.sln` open in your IDE, open `GreeterServer/Program.cs`. Implement the new method by editing the GreeterImpl class like this:
 
```
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

### Update the client

With the same `Greeter.sln` open in your IDE, open `GreeterClient/Program.cs`. Call the new method like this:

```
public static void Main(string[] args)
{
    Channel channel = new Channel("127.0.0.1:50051", ChannelCredentials.Insecure);

    var client = new Greeter.GreeterClient(channel);
    String user = "you";

    var reply = client.SayHello(new HelloRequest { Name = user });
    Console.WriteLine("Greeting: " + reply.Message);
    
    var reply = client.SayHelloAgain(new HelloRequest { Name = user });
    Console.WriteLine("Greeting: " + reply.Message);

    channel.ShutdownAsync().Wait();
    Console.WriteLine("Press any key to exit...");
    Console.ReadKey();
}
```

### Rebuild the modified example

Rebuild the newly modified example just like we first built the original example:

* With solution Greeter.sln open from Visual Studio, Monodevelop (on Linux) or Xamarin Studio (on Mac OS X)
* Build the solution 

### Run!

Just like we did before, from the `examples/csharp/helloworld` directory:

1. Run the server

```
> cd GreeterServer/bin/Debug
> GreeterServer.exe
```

2. In another terminal, run the client

```
> cd GreeterClient/bin/Debug
> GreeterClient.exe
```

## What's next

- Read a full explanation of this example and how gRPC works in our [Overview](http://www.grpc.io/docs/)
- Work through a more detailed tutorial in [gRPC Basics: C#][]
- Explore the gRPC C# core API in its [reference documentation](http://www.grpc.io/grpc/csharp/)

[gRPC Linuxbrew instructions]:https://github.com/grpc/homebrew-grpc#quick-install-linux
[gRPC Homebrew instructions]:https://github.com/grpc/homebrew-grpc#quick-install-linux
[helloworld.proto]:../protos/helloworld.proto
[gRPC Basics: C#]:http://www.grpc.io/docs/tutorials/basic/csharp.html
