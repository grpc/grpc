[![Nuget](https://img.shields.io/nuget/v/Grpc.svg)](http://www.nuget.org/packages/Grpc/)
gRPC C#
=======

A C# implementation of gRPC.

SUPPORTED PLATFORMS
------------------

- [.NET Core](https://dotnet.github.io/) on Linux, Windows and Mac OS X 
- .NET Framework 4.5+ (Windows)
- Mono 4+ on Linux, Windows and Mac OS X

PREREQUISITES
--------------

When using gRPC C# under .NET Core you only need to [install .NET Core](https://www.microsoft.com/net/core).

- Windows: .NET Framework 4.5+, Visual Studio 2013, 2015, 2017
- Linux: Mono 4+, MonoDevelop 5.9+ (with NuGet add-in installed)
- Mac OS X: Xamarin Studio 5.9+

HOW TO USE
--------------

**Windows, Linux, Mac OS X**

- Open Visual Studio / MonoDevelop / Xamarin Studio and start a new project/solution.

- Add the [Grpc](https://www.nuget.org/packages/Grpc/) NuGet package as a dependency (Project options -> Manage NuGet Packages). 

- To be able to generate code from Protocol Buffer (`.proto`) file definitions, add the [Grpc.Tools](https://www.nuget.org/packages/Grpc.Tools/) NuGet package that contains Protocol Buffers compiler (_protoc_) and the gRPC _protoc_ plugin.

BUILD FROM SOURCE
-----------------

You only need to go through these steps if you are planning to develop gRPC C#.
If you are a user of gRPC C#, go to Usage section above.

**Windows, Linux or Mac OS X**

- The easiest way to build is using the `run_tests.py` script that will take care of building the `grpc_csharp_ext` native library:
  ```
  # from the gRPC repository root
  $ python tools/run_tests/run_tests.py -c dbg -l csharp --build_only
  ```

- Use Visual Studio 2017 (on Windows) to open the solution `Grpc.sln` or use Visual Studio Code with C# extension (on Linux and Mac). gRPC C# code has been migrated to
  dotnet SDK `.csproj` projects that are much simpler to maintain, but are not yet supported by Xamarin Studio or Monodevelop (the NuGet packages still
  support both `net45` and `netstandard` and can be used in all IDEs).

RUNNING TESTS
-------------

gRPC C# is using NUnit as the testing framework.

Under Visual Studio, make sure NUnit test adapter is installed (under "Extensions and Updates").
Then you should be able to run all the tests using Test Explorer.

gRPC team uses a Python script to simplify facilitate running tests for
different languages.

```
tools/run_tests/run_tests.py -l csharp
```

DOCUMENTATION
-------------
- [API Reference][]
- [Helloworld Example][]
- [RouteGuide Tutorial][]

PERFORMANCE
-----------

For best gRPC C# performance, use [.NET Core](https://dotnet.github.io/) and the Server GC mode `"System.GC.Server": true` for your applications.

THE NATIVE DEPENDENCY
---------------

Internally, gRPC C# uses a native library written in C (gRPC C core) and invokes its functionality via P/Invoke. The fact that a native library is used should be fully transparent to the users and just installing the `Grpc.Core` NuGet package is the only step needed to use gRPC C# on all supported platforms.

[API Reference]: https://grpc.io/grpc/csharp/
[Helloworld Example]: ../../examples/csharp/helloworld
[RouteGuide Tutorial]: https://grpc.io/docs/tutorials/basic/csharp.html 
