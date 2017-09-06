gRPC in 3 minutes (C#)
========================

BACKGROUND
-------------
For this sample, we've already generated the server and client stubs from [helloworld.proto][].

Example projects depend on the [Grpc](https://www.nuget.org/packages/Grpc/), [Grpc.Tools](https://www.nuget.org/packages/Grpc.Tools/)
and [Google.Protobuf](https://www.nuget.org/packages/Google.Protobuf/) NuGet packages
which have been already added to the project for you.

PREREQUISITES
-------------

- Windows: .NET Framework 4.5+, Visual Studio 2013 or 2015
- Linux: Mono 4+, MonoDevelop 5.9+
- Mac OS X: Xamarin Studio 5.9+

BUILD
-------

- Open solution `Greeter.sln` with Visual Studio, Monodevelop (on Linux) or Xamarin Studio (on Mac OS X)

# Using Visual Studio

* Build the solution (this will automatically download NuGet dependencies)

# Using Monodevelop or Xamarin Studio

The nuget add-in available for Xamarin Studio and Monodevelop IDEs is too old to 
download all of the nuget dependencies of gRPC. One alternative to is to use the dotnet command line tools instead (see [helloworld-from-cli]).

Using these IDEs, a workaround is as follows:
* Obtain a nuget executable for your platform and update it with
 `nuget update -self`. 
* Navigate to this directory and run `nuget restore`.
* Now that packages have been restored into their proper package folder, build the solution from your IDE.

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

[helloworld-from-cli]:../helloworld-from-cli/README.md
[helloworld.proto]:../../protos/helloworld.proto
[gRPC Basics: C#]:https://grpc.io/docs/tutorials/basic/csharp.html
