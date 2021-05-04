[![Nuget](https://img.shields.io/nuget/v/Grpc.svg)](http://www.nuget.org/packages/Grpc/)
gRPC C#
=======

A C# implementation of gRPC based on the native gRPC Core library.

The implementation in this directory is the original implementation of gRPC for C# (i.e. `Grpc.Core` nuget package)
and it is currently in maintenance mode. We plan to deprecate it in the future
in favor of the [grpc-dotnet](https://github.com/grpc/grpc-dotnet) implementation.
See [blogpost](https://grpc.io/blog/grpc-csharp-future/) for more details.

The following documentation is for the original gRPC C# implementation only (the `Grpc.Core` nuget package).

SUPPORTED PLATFORMS
------------------

- [.NET Core](https://dotnet.github.io/) on Linux, Windows and Mac OS X
- .NET Framework 4.5+ (Windows)
- Mono 4+ on Linux, Windows and Mac OS X

PREREQUISITES
--------------

When using gRPC C# under .NET Core you only need to [install .NET Core](https://www.microsoft.com/net/core).

In addition to that, you can also use gRPC C# with these runtimes / IDEs
- Windows: .NET Framework 4.5+, Visual Studio 2013 or newer, Visual Studio Code
- Linux: Mono 4+, Visual Studio Code
- Mac OS X: Mono 4+, Visual Studio Code, Visual Studio for Mac

HOW TO USE
--------------

**Windows, Linux, Mac OS X**

- Open Visual Studio and start a new project/solution (alternatively, you can create a new project from command line with `dotnet` SDK)

- Add the [Grpc](https://www.nuget.org/packages/Grpc/) NuGet package as a dependency (Project options -> Manage NuGet Packages).

- To be able to generate code from Protocol Buffer (`.proto`) file definitions, add the [Grpc.Tools](https://www.nuget.org/packages/Grpc.Tools/) NuGet package which provides [code generation integrated into your build](BUILD-INTEGRATION.md).

**Xamarin.Android and Xamarin.iOS (Experimental only)**

See [Experimentally supported platforms](experimental) for instructions.

**Unity (Experimental only)**

See [Experimentally supported platforms](experimental) for instructions.

NUGET DEVELOPMENT FEED (NIGHTLY BUILDS)
--------------

In production, you should use officially released stable packages available on http://nuget.org, but if you want to test the newest upstream bug fixes and features early, you can use the development nuget feed where new nuget builds are uploaded nightly.

Feed URL (NuGet v2): https://grpc.jfrog.io/grpc/api/nuget/grpc-nuget-dev

Feed URL (NuGet v3): https://grpc.jfrog.io/grpc/api/nuget/v3/grpc-nuget-dev

The same development nuget packages and packages for other languages can also be found at https://packages.grpc.io/

BUILD FROM SOURCE
-----------------

You only need to go through these steps if you are planning to develop gRPC C#.
If you are a user of gRPC C#, go to Usage section above.

**Prerequisites for contributors**

- [dotnet SDK](https://www.microsoft.com/net/core)
- [Mono 4+](https://www.mono-project.com/) (only needed for Linux and MacOS)
- Prerequisites mentioned in [BUILDING.md](../../BUILDING.md#pre-requisites)
  to be able to compile the native code.

**Windows, Linux or Mac OS X**

- The easiest way to build is using the `run_tests.py` script that will take care of building the `grpc_csharp_ext` native library.

  ```
  # NOTE: make sure all necessary git submodules with dependencies
  # are available by running "git submodule update --init"

  # from the gRPC repository root
  $ python tools/run_tests/run_tests.py -l csharp -c dbg --build_only
  ```

- Use Visual Studio 2017 (on Windows) to open the solution `Grpc.sln` or use Visual Studio Code with C# extension (on Linux and Mac). gRPC C# code has been migrated to
  dotnet SDK `.csproj` projects that are much simpler to maintain, but are not yet supported by Xamarin Studio or Monodevelop (the NuGet packages still
  support both `net45` and `netstandard` and can be used in all IDEs).

RUNNING TESTS
-------------

gRPC C# is using NUnit as the testing framework.

Under Visual Studio, make sure NUnit test adapter is installed (under "Extensions and Updates").
Then you should be able to run all the tests using Test Explorer.

gRPC team uses a Python script to facilitate running tests for
different languages.

```
# from the gRPC repository root
$ python tools/run_tests/run_tests.py -l csharp -c dbg
```

DOCUMENTATION
-------------
- [.NET Build Integration](BUILD-INTEGRATION.md)
- [API Reference][]
- [Helloworld Example][]
- [RouteGuide Tutorial][]

PERFORMANCE
-----------

For best gRPC C# performance, use [.NET Core](https://dotnet.github.io/) and the Server GC mode `"System.GC.Server": true` for your applications.

THE NATIVE DEPENDENCY
---------------

Internally, gRPC C# uses a native library written in C (gRPC C core) and invokes its functionality via P/Invoke. The fact that a native library is used should be fully transparent to the users and just installing the `Grpc.Core` NuGet package is the only step needed to use gRPC C# on all supported platforms.

[API Reference]: https://grpc.io/grpc/csharp/api/Grpc.Core.html
[Helloworld Example]: ../../examples/csharp/Helloworld
[RouteGuide Tutorial]: https://grpc.io/docs/languages/csharp/basics
