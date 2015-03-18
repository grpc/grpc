gRPC C#
=======

A C# implementation of gRPC.

Status
-----------------

**This gRPC C# implementation is work-in-progress and is not expected to work yet.**

- The implementation is a wrapper around gRPC C core library
- Code only runs under mono currently, building gRPC C core library under Windows
  is in progress.
- It is very possible that some parts of the code will be heavily refactored or
  completely rewritten.


INSTALLATION AND USAGE: WINDOWS
-------------------------------

- Open Grpc.sln using Visual Studio 2013. NuGet dependencies will be restored
  upon build.


INSTALLATION AND USAGE: LINUX & MONO
------------------------------------

- Compile and install the gRPC C# extension library (that will be used via
  P/Invoke from C#).
```
make grpc_csharp_ext
sudo make install_grpc_csharp_ext
```

- Prerequisites for development: Mono framework, MonoDevelop (IDE)
```
sudo apt-get install mono-devel
sudo apt-get install monodevelop monodevelop-nunit
sudo apt-get install nunit nunit-console
```

- NuGet is used to manage project's dependencies. Prior opening Grpc.sln,
  download dependencies using NuGet restore command:

```
# Import needed certicates into Mono certificate store:
mozroots --import --sync

# Download NuGet.exe http://nuget.codeplex.com/releases/
# Restore the nuget packages with Grpc C# dependencies
mono ~/Downloads/NuGet.exe restore Grpc.sln
```

- Use MonoDevelop to open the solution Grpc.sln (you can also run unit tests
  from there).

- After building the solution with MonoDevelop, you can use
  nunit-console to run the unit tests (currently only running one by
  one will make them pass.

```
nunit-console Grpc.Core.Tests.dll
```

CONTENTS
--------

- ext:
  The extension library that wraps C API to be more digestible by C#.
- Grpc.Core:
  The main gRPC C# library.
- Grpc.Examples:
  API examples for math.proto
- Grpc.Examples.MathClient:
  An example client that sends some requests to math server.
- Grpc.IntegrationTesting:
  Client for cross-language gRPC implementation testing (interop testing).
