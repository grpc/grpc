gRPC in 3 minutes (C#)
========================

BACKGROUND
-------------
For this sample, we've already generated the server and client stubs from [helloworld.proto][].

Example projects depend on the [Grpc](https://www.nuget.org/packages/Grpc/), [Grpc.Tools](https://www.nuget.org/packages/Grpc.Tools/)
and [Google.Protobuf](https://www.nuget.org/packages/Google.Protobuf/) NuGet packages
which have been already added to the project for you.

The [gRPC C# quickstart](http://http://www.grpc.io/docs/quickstart/csharp.html) provides a walkthrough of the "Hello World!" example
provided in this directory, and the "Hello World!" example that uses the dotnet
cli in [helloworld-from-cli], and the most detailed pre-req, build, and run
instructions can be found in this guide. If you are only interested in building
and running a project that uses C# gRPC, you can skip the protos generation part
of the quickstart and just follow the sections involving pre-reqs, building, and
running.

PREREQUISITES
-------------

See the [pre-req section](http://http://www.grpc.io/docs/quickstart/csharp.html#prerequisites) of the gRPC C# quickstart.

BUILD
-------

See the [build section](http://www.grpc.io/docs/quickstart/csharp.html#build-the-example) of the C# gRPC quickstart.

Try it!
-------

See the [run section](http://www.grpc.io/docs/quickstart/csharp.html#run-a-grpc-application) of the C# gRPC quickstart

Tutorial
--------

You can find a more detailed tutorial in [gRPC Basics: C#][]

[helloworld-from-cli]:../helloworld-from-cli/README.md
[helloworld.proto]:../../protos/helloworld.proto
[gRPC Basics: C#]:http://www.grpc.io/docs/tutorials/basic/csharp.html
