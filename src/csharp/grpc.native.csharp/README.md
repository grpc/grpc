gRPC Native Nuget package
=========================

Prerequisites
-------------

NuGet binary

Building the package
--------------------

To build the native package, you need precompiled versions
of grpc_csharp_ext library artifacts for Windows, Linux and Mac.
In the normal gRPC release process, these are built by a Jenkins
job and they are copied to the expected location before building
the native nuget package is attempted.

See tools/run_tests/build_artifacts.py for more details how
precompiled artifacts are built.

When building the native NuGet package, ignore the "Assembly outside lib folder"
warnings (the DLLs are not assemblies, they are native libraries).
