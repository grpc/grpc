gRPC C#
=======

A C# implementation of gRPC.

Status
------

Ready for early adopters.

Usage: Windows
--------------

- Prerequisites: .NET Framework 4.5+, Visual Studio 2013 with NuGet extension installed (VS2015 should work).

- Open Visual Studio and start a new project/solution.

- Add NuGet package `Grpc` as a dependency (Project options -> Manage NuGet Packages).
  That will also pull all the transitive dependencies (including the native libraries that
  gRPC C# is internally using).

- TODO: link to Helloworld example

Usage: Linux (Mono)
--------------

- Prerequisites: Mono framework, MonoDevelop 5.9 with NuGet add-in installed.

- Install gRPC C Core using instructions in https://github.com/grpc/homebrew-grpc

- TODO: explain using LD_LIBRARY_PATH or installation to /usr/local

- Open MonoDevelop and start a new project/solution.

- Add NuGet package `Grpc` as a dependency (Project -> Add NuGet packages).

- TODO: link to Helloworld example

Building: Windows
-----------------

You only need to go through these steps if you are planning to develop gRPC C#.
If you are a user of gRPC C#, go to Usage section above.

- Prerequisites for development: NET Framework 4.5+, Visual Studio 2013 (with NuGet and NUnit extensions installed).

- The grpc_csharp_ext native library needs to be built so you can build the Grpc C# solution. You can 
  either build the native solution in `vsprojects/grpc.sln` from Visual Studio manually, or you can use
  a convenience batch script that builds everything for you.
```
buildall.bat
```

- Open Grpc.sln using Visual Studio 2013. NuGet dependencies will be restored
  upon build (you need to have NuGet add-in installed).


Building: Linux & Mono
----------------------

You only need to go through these steps if you are planning to develop gRPC C#.
If you are a user of gRPC C#, go to Usage section above.

- Prerequisites for development: Mono framework, MonoDevelop 5.9 with NuGet and Nunit add-ins installed.
```
sudo apt-get install mono-devel
sudo apt-get install nunit nunit-console
```

You can use older versions of MonoDevelop, but then you might need to restore
NuGet dependencies manually (by `nuget restore`), because older versions of MonoDevelop
don't support NuGet add-in.

- Compile and install the gRPC C# extension library (that will be used via
  P/Invoke from C#).
```
make grpc_csharp_ext
sudo make install_grpc_csharp_ext
```

- Use MonoDevelop to open the solution Grpc.sln

- Build the solution & run all the tests from test view.

Tests
-----

gRPC C# is using NUnit as the testing framework.

Under Visual Studio, make sure NUnit test adapter is installed (under "Extensions and Updates").
Then you should be able to run all the tests using Test Explorer.

Under Monodevelop, make sure you installed "NUnit support" in Add-in manager.
Then you should be able to run all the test from the Test View.

After building the solution, you can also run the tests from command line 
using nunit-console tool.

```
# from Grpc.Core.Test/bin/Debug directory
nunit-console Grpc.Core.Tests.dll
```

Contents
--------

- ext:
  The extension library that wraps C API to be more digestible by C#.
- Grpc.Auth:
  gRPC OAuth2 support.
- Grpc.Core:
  The main gRPC C# library.
- Grpc.Examples:
  API examples for math.proto
- Grpc.Examples.MathClient:
  An example client that sends some requests to math server.
- Grpc.Examples.MathServer:
  An example client that sends some requests to math server.
- Grpc.IntegrationTesting:
  Cross-language gRPC implementation testing (interop testing).
