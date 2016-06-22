[![Nuget](https://img.shields.io/nuget/v/Grpc.svg)](http://www.nuget.org/packages/Grpc/)
gRPC C#
=======

A C# implementation of gRPC.

Status
------

Beta

PREREQUISITES
--------------

- Windows: .NET Framework 4.5+, Visual Studio 2013 or 2015
- Linux: Mono 4+, MonoDevelop 5.9+ (with NuGet add-in installed)
- Mac OS X: Xamarin Studio 5.9+

HOW TO USE
--------------

**Windows, Linux, Mac OS X**

- Open Visual Studio / MonoDevelop / Xamarin Studio and start a new project/solution.

- Add NuGet package `Grpc` as a dependency (Project options -> Manage NuGet Packages).

- To be able to generate code from Protocol Buffer (`.proto`) file definitions, add NuGet package `Grpc.Tools` that contains Protocol Buffers compiler (_protoc_) and the gRPC _protoc_ plugin.

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

Internally, gRPC C# uses a native library written in C (gRPC C core) and invokes its functionality via P/Invoke. `grpc_csharp_ext` library is a native extension library that facilitates this by wrapping some C core API into a form that's more digestible for P/Invoke.

Prior to version 0.13, installing `grpc_csharp_ext` was required to make gRPC work on Linux and MacOS. Starting with version 0.13, we have improved the packaging story significantly and precompiled versions of the native library for all supported platforms are now shipped with the NuGet package. Just installing the `Grpc` NuGet package should be the only step needed to use gRPC C#, regardless of your platform (Windows, Linux or Mac) and the bitness (32 or 64bit).

[API Reference]: http://www.grpc.io/grpc/csharp/
[Helloworld Example]: ../../examples/csharp/helloworld
[RouteGuide Tutorial]: http://www.grpc.io/docs/tutorials/basic/csharp.html 
