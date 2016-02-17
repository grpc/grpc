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
- Mono 4.0+
- Monodevelop 5.9+ (with NuGet plugin installed)

**Mac OS X**
- Xamarin Studio 5.9+
- [homebrew][]

BUILD
-------

- Open solution `Greeter.sln` with Visual Studio, Monodevelop (on Linux) or Xamarin Studio (on Mac OS X)

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

You can also run the server and client directly from the IDE.

On Linux or Mac, use `mono GreeterServer.exe` and `mono GreeterClient.exe` to run the server and client.

Tutorial
--------

You can find a more detailed tutorial in [gRPC Basics: C#][]

[homebrew]:http://brew.sh
[helloworld.proto]:../../protos/helloworld.proto
[gRPC Basics: C#]:http://www.grpc.io/docs/tutorials/basic/csharp.html
