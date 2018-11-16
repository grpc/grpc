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

- The [.NET Core SDK 2.1+](https://www.microsoft.com/net/core)

You can also build the example directly using Visual Studio 2017, but it's not a requirement.

BUILD
-------

From the `examples/csharp/Helloworld` directory:

- `dotnet build Greeter.sln`

Try it!
-------

- Run the server

  ```
  > cd GreeterServer
  > dotnet run -f netcoreapp2.1
  ```

- Run the client

  ```
  > cd GreeterClient
  > dotnet run -f netcoreapp2.1
  ```

Tutorial
--------

You can find a more detailed tutorial about Grpc in [gRPC Basics: C#][]

[helloworld.proto]:../../protos/helloworld.proto
[gRPC Basics: C#]:https://grpc.io/docs/tutorials/basic/csharp.html
