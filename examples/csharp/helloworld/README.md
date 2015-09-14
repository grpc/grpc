gRPC in 3 minutes (C#)
========================

BACKGROUND
-------------
For this sample, we've already generated the server and client stubs from `helloworld.proto`. 
Example projects depend on NuGet packages `Grpc` and `Google.ProtocolBuffers` which have been already added to the project for you.

PREREQUISITES
-------------
**Windows**
- .NET 4.5+
- VS 2013 (with NuGet plugin installed)

**Linux (Mono)**
- Mono
- Monodevelop 5.9 with NuGet Add-in installed (older versions might work)

**MacOS (Mono)**
- Xamarin Studio (with NuGet plugin installed)

BUILD
-------

**Windows**
- Clone this repository.

- Open solution `Greeter.sln` with Visual Studio

- Build the solution (this will automatically download NuGet dependencies)

**Linux (Mono)**
- Clone this repository.

- Install gRPC C Core using instructions in https://github.com/grpc/homebrew-grpc

- gRPC C# depends on native shared library `libgrpc_csharp_ext.so`. To make it visible
  to Mono runtime, follow instructions in [Using gRPC C# on Linux](https://github.com/grpc/grpc/tree/master/src/csharp#usage-linux-mono)

- Open solution `Greeter.sln` in MonoDevelop (you need to manually restore dependencies by using `mono nuget.exe restore` if you don't have NuGet add-in)

- Build the solution.

**MacOS (Mono)**
- See [Using gRPC C# on MacOS](https://github.com/grpc/grpc/tree/master/src/csharp#usage-macos-mono) for more info
  on MacOS support.

Try it! 
-------

- Run the server

  ```
  > cd GreeterServer/bin/Debug
  > GreeterServer.exe
  ```

- Run the client

  ```
  > cd GreeterClient/bin/Debug
  > GreeterClient.exe
  ```

You can also run the server and client directly from Visual Studio.

On Linux or Mac, use `mono GreeterServer.exe` and `mono GreeterClient.exe` to run the server and client.

Tutorial
--------

You can find a more detailed tutorial in [gRPC Basics: C#](route_guide/README.md)
