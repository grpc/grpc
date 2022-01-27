gRPC in 3 minutes (C#)
========================

BACKGROUND
-------------
This is a different version of the helloworld example, using the "classic" .csproj
files, the only format supported by VS2013 (and older versions of mono).
You can still use gRPC with the classic .csproj files, but [using the new-style
.csproj projects](../Helloworld/README.md) (supported by VS2017 v15.3 and above,
and dotnet SDK) is recommended.

Example projects depend on the [Grpc](https://www.nuget.org/packages/Grpc/),
[Grpc.Tools](https://www.nuget.org/packages/Grpc.Tools/)
and [Google.Protobuf](https://www.nuget.org/packages/Google.Protobuf/) NuGet packages
which have been already added to the project for you.

PREREQUISITES
-------------

- Windows: .NET Framework 4.5+, Visual Studio 2013 or higher
- Linux: Mono 4+, MonoDevelop 5.9+
- Mac OS X: Xamarin Studio 5.9+

BUILD
-------

- Open solution `Greeter.sln` with Visual Studio, Monodevelop (on Linux) or Xamarin Studio (on Mac OS X)

# Using Visual Studio

* Select "Restore NuGet Packages" from the solution context menu. It is recommended
  to close and re-open the solution after the packages have been restored from
  Visual Studio.
* Build the solution.

# Using Monodevelop or Xamarin Studio

The NuGet add-in available for Xamarin Studio and Monodevelop IDEs is too old to
download all of the NuGet dependencies of gRPC.

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

[helloworld.proto]:../../protos/helloworld.proto
[gRPC Basics: C#]:https://grpc.io/docs/languages/csharp/basics
