gRPC in 3 minutes (C#)
========================

BACKGROUND
-------------
For this sample, we've already generated the server and client stubs from [helloworld.proto][].

Example projects depend on the [Grpc](https://www.nuget.org/packages/Grpc/)
and [Google.Protobuf](https://www.nuget.org/packages/Google.Protobuf/) NuGet packages
which have been already added to the project for you.

PREREQUISITES
-------------
**Windows**
- .NET 4.5+
- Visual Studio 2013 or 2015

**Linux**
- Mono
- Monodevelop 5.9 with NuGet Add-in installed

**Mac OS X**
- Xamarin Studio (with NuGet plugin installed)
- [homebrew][]

BUILD
-------

**Windows**

- Open solution `Greeter.sln` with Visual Studio

- Build the solution (this will automatically download NuGet dependencies)

**Linux (Debian)**

- Install gRPC C core and C# native extension using [How to use gRPC C#][] instructions

- Open solution `Greeter.sln` in MonoDevelop.

- Build the solution (you need to manually restore dependencies by using `mono nuget.exe restore` if you don't have NuGet add-in)

**Mac OS X**

- Install gRPC C core and C# native extension using [How to use gRPC C#][] instructions

- Open solution `Greeter.sln` with Xamarin Studio

- Build the solution (this will automatically download NuGet dependencies)

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

You can find a more detailed tutorial in [gRPC Basics: C#][]

[homebrew]:http://brew.sh
[helloworld.proto]:../../protos/helloworld.proto
[How to use gRPC C#]:../../../src/csharp#how-to-use
[gRPC Basics: C#]:http://www.grpc.io/docs/tutorials/basic/csharp.html
