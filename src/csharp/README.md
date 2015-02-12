gRPC C#
=======

A C# implementation of gRPC, Google's RPC library.

EXPERIMENTAL ONLY
-----------------

**This gRPC C# implementation is work-in-progress and is not expected to work yet.**

- The implementation is a wrapper around gRPC C core library
- Code only runs under mono currently, building gGRPC C core library under Windows
  is in progress.
- It is very possible that some parts of the code will be heavily refactored or
  completely rewritten.


INSTALLATION AND USAGE: WINDOWS
-------------------------------

- Open Grpc.sln using Visual Studio 2013. NuGet dependencies will be restored
  upon build.


INSTALLATION AND USAGE: LINUX & MONO
------------------------------------

- Compile and install the gRPC C Core library
```
make shared_c
sudo make install
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
nunit-console GrpcCoreTests.dll
```

CONTENTS
--------

- ext:
  The extension library that wraps C API to be more digestible by C#.
- GrpcApi:
  API examples for math.proto and testservice.proto
- GrpcCore:
  The main gRPC C# library.
- InteropClient:
  Client for interop testing.
- MathClient:
  An example client that sends some requests to math server.
