[![Nuget](https://img.shields.io/nuget/v/Grpc.svg)](http://www.nuget.org/packages/Grpc/)
gRPC C#
=======

A C# implementation of gRPC.

SUPPORTED PLATFORMS
------------------

- .NET Framework 4.5+ (Windows)
- [.NET Core](https://dotnet.github.io/) on Linux, Windows and Mac OS X (starting from version 1.0.1)
- Mono 4+ on Linux, Windows and Mac OS X


PREREQUISITES
--------------

- Windows: .NET Framework 4.5+, Visual Studio 2013 or 2015
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

**Windows**

- The grpc_csharp_ext native library needs to be built so you can build the gRPC C# solution. Open the
  solution `vsprojects/grpc_csharp_ext.sln` in Visual Studio and build it.

- Open `src\csharp\Grpc.sln` (path is relative to gRPC repository root)
  using Visual Studio

**Linux and Mac OS X**

- The grpc_csharp_ext native library needs to be built so you can build the gRPC C# solution:
  ```sh
  # from the gRPC repository root
  $ tools/run_tests/run_tests.py -c dbg -l csharp --build_only
  ```

- Use MonoDevelop / Xamarin Studio to open the solution Grpc.sln

RUNNING TESTS
-------------

gRPC C# is using NUnit as the testing framework.

Under Visual Studio, make sure NUnit test adapter is installed (under "Extensions and Updates").
Then you should be able to run all the tests using Test Explorer.

Under Monodevelop or Xamarin Studio, make sure you installed "NUnit support" in Add-in manager.
Then you should be able to run all the test from the Test View.

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

CONTENTS
--------

- ext:
  The extension library that wraps C API to be more digestible by C#.
- Grpc.Auth:
  gRPC OAuth2/JWT support.
- Grpc.Core:
  The main gRPC C# library.
- Grpc.Examples:
  API examples for math.proto
- Grpc.Examples.MathClient:
  An example client that sends requests to math server.
- Grpc.Examples.MathServer:
  An example server that implements a simple math service.
- Grpc.IntegrationTesting:
  Cross-language gRPC implementation testing (interop testing).

THE NATIVE DEPENDENCY
---------------

Internally, gRPC C# uses a native library written in C (gRPC C core) and invokes its functionality via P/Invoke. The fact that a native library is used should be fully transparent to the users and just installing the `Grpc.Core` NuGet package is the only step needed to use gRPC C# on all supported platforms.

[API Reference]: http://www.grpc.io/grpc/csharp/
[Helloworld Example]: ../../examples/csharp/helloworld
[RouteGuide Tutorial]: http://www.grpc.io/docs/tutorials/basic/csharp.html 
