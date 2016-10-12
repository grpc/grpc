gRPC in 3 minutes (C#)
========================

BACKGROUND
-------------
This is a different version of the helloworld example, using the dotnet sdk
tools to build and run.

For this sample, we've already generated the server and client stubs from [helloworld.proto][].

Example projects in this directory depend on the [Grpc](https://www.nuget.org/packages/Grpc/)
and [Google.Protobuf](https://www.nuget.org/packages/Google.Protobuf/) NuGet packages
which have been already added to the project for you.

The examples in this directory target .NET 4.5 framework, as .NET Core support is
currently experimental.

PREREQUISITES
-------------

- The DotNetCore SDK cli.

- The .NET 4.5 framework.

Both are available to download at https://www.microsoft.com/net/download

BUILD
-------

From the `examples/csharp/helloworld-from-cli` directory:

- `dotnet restore`

- `dotnet build **/project.json` (this will automatically download NuGet dependencies)

Try it!
-------

- Run the server

  ```
  > cd GreeterServer
  > dotnet run
  ```

- Run the client

  ```
  > cd GreeterClient
  > dotnet run
  ```

Tutorial
--------

You can find a more detailed tutorial about Grpc in [gRPC Basics: C#][]

[helloworld.proto]:../../protos/helloworld.proto
[gRPC Basics: C#]:http://www.grpc.io/docs/tutorials/basic/csharp.html
