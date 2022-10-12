gRPC C#
=======

This directory used to contain the original C# implementation of gRPC based on the native gRPC Core library
(i.e. `Grpc.Core` nuget package). This implementation is currently in maintenance mode and its source code
has been moved (see below for details). Also, we plan to deprecate the implementation in the future
(see [blogpost](https://grpc.io/blog/grpc-csharp-future/)).

The source code that used to reside here has been moved as following:

- The `Grpc`, `Grpc.Core`, `Grpc.Core.Testing`, `Grpc.Core.NativeDebug` and `Grpc.Core.Xamarin` packages will continue to live in maintenance mode on the [v1.46.x](https://github.com/grpc/grpc/tree/v1.46.x) branch, where they will get the necessary critical/security patches as needed and will be released from there (as usual for gRPC patch releases). All future releases of these packages will have their major/minor version fixed at `2.46` and only their patch version will be incremented (`2.46.0`,`2.46.1`, ...)

- The `Grpc.Core.Api`, `Grpc.Auth`, `Grpc.HealthCheck` and `Grpc.Reflection` packages will be moved to the [grpc-dotnet repository](https://github.com/grpc/grpc-dotnet) where their development will continue (note that all these packages are implementation-agnostic, and they are also used by grpc-dotnet, so it makes sense to move them there). Future releases of these packages (`v2.47.x`, `v2.48.x`, ...) will be released as part of the [grpc-dotnet release](https://github.com/grpc/grpc-dotnet/blob/master/doc/release_process.md).

- The package `Grpc.Tools` (which provides the protobuf/grpc codegen build integration) will for now stay on the master branch of grpc/grpc (i.e. in this directory). From there it will continue to be released along with other gRPC languages that live in the `grpc/grpc` repository. The eventual goal is to also move Grpc.Tools to elsewhere (probably the grpc-dotnet repository), but more work is needed there (e.g. we need to figure out some technical and test-infrastructure related issues first).

The original `src/csharp` tree
---------------

It currently lives on the `v1.46.x` release branch here: https://github.com/grpc/grpc/tree/v1.46.x/src/csharp (and is in maintenance mode).

The original gRPC C# examples
---------------

The examples for the `Grpc.Code` implementation of gRPC for C# can be found here: https://github.com/grpc/grpc/tree/v1.46.x/examples/csharp
