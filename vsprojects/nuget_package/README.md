gRPC Native Nuget package
=========================

Prerequisites
-------------
Multiple versions of VS installed to be able to build all the targets:
* Visual Studio 2013
* Visual Studio 2010 (you might need SP1 to prevent LNK1123 error)

NuGet binary

Building the package
--------------------

Build all flavors of gRPC C# extension and package them as a NuGet package.
```
buildall.bat

nuget pack grpc.native.csharp.nuspec
```

When building the NuGet package, ignore the "Assembly outside lib folder" warnings (they DLLs are not assemblies, they are native libraries).
