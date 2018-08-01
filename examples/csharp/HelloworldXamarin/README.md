gRPC C# on Xamarin
========================

EXPERIMENTAL ONLY
-------------
Support of the Xamarin platform is currently experimental.
The example depends on experimental Grpc.Core nuget package that hasn't
been officially released and is only available via the [daily builds](https://packages.grpc.io/)
source.

BACKGROUND
-------------
The example project supports Xamarin.Android and Xamarin.iOS

For this sample, we've already generated the server and client stubs from [helloworld.proto][].

PREREQUISITES
-------------

- The latest version Xamarin Studio or Visual Studio 2017 with Xamarin support installed.

BUILD
-------

- Open the `HelloworldXamarin.sln` in Visual Studio (or Xamarin Studio)
- Build the solution (Build -> Build All)

Try it!
-------

You can deploy the example apps directly through Xamarin Studio IDE.
Deployments can target both Android and iOS (both support physical device
deployment as well as simulator).

[helloworld.proto]:../../protos/helloworld.proto
