gRPC in 3 minutes (C#)
========================

BACKGROUND
-------------
For this sample, we've already generated the server and client stubs from [helloworld.proto][].

Example projects in this directory depend on the [Grpc](https://www.nuget.org/packages/Grpc/)
and [Google.Protobuf](https://www.nuget.org/packages/Google.Protobuf/) NuGet packages
which have been already added to the project for you.

PREREQUISITES
-------------

- The [.NET Core SDK](https://www.microsoft.com/net/core) (version 2+ is recommended)

You can also build the example directly using Visual Studio 2017, but it's not a requirement.

BUILD
-------

From the `examples/csharp/Helloworld` directory:

- `dotnet build Greeter.sln`

(if you're using dotnet SDK 1.x you need to run `dotnet restore Greeter.sln` first)

Try it!
-------

- Run the server

  ```
  > cd GreeterServer
  > dotnet run -f netcoreapp1.0
  ```

- Run the client

  ```
  > cd GreeterClient
  > dotnet run -f netcoreapp1.0
  ```

Tutorial
--------

You can find a more detailed tutorial about Grpc in [gRPC Basics: C#][]

[helloworld.proto]:../../protos/helloworld.proto
[gRPC Basics: C#]:https://grpc.io/docs/tutorials/basic/csharp.html
