gRPC C# on Xamarin
========================

EXPERIMENTAL ONLY
-------------
Support of the Xamarin platform is currently experimental.
The example depends on experimental Grpc.Core nuget package that hasn't
been officially released and is only available via the [daily builds](https://packages.grpc.io/)
source.

HINT: To download the package, please manually download the latest `.nupkg` packages from "Daily Builds" in [packages.grpc.io](https://packages.grpc.io/) into a local directory. Then add a nuget source that points to that directory (That can be [done in Visual Studio](https://docs.microsoft.com/en-us/nuget/tools/package-manager-ui#package-sources) or Visual Studio for Mac via "Configure nuget sources"). After that, nuget will also explore that directory when looking for packages.

BACKGROUND
-------------
The example project supports `Xamarin.Android` and `Xamarin.iOS`.

For this sample, we've already generated the server and client stubs from [helloworld.proto][].

PREREQUISITES
-------------

- The latest version Visual Studio 2017 or Visual Studio for Mac with Xamarin support installed.

BUILD
-------

- Open the `HelloworldXamarin.sln` in Visual Studio (or Visual Studio for Mac)
- Build the solution (Build -> Build All)

Try it!
-------

You can deploy the example apps directly through Visual Studio IDE.
Deployments can target both Android and iOS (both support physical device
deployment as well as simulator).

[helloworld.proto]:../../protos/helloworld.proto
