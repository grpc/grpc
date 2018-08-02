gRPC C# on Xamarin
========================

EXPERIMENTAL ONLY
-------------
Support of the Xamarin platform is currently experimental.
The example depends on experimental Grpc.Core nuget package that hasn't
been officially released and is only available via the [daily builds](https://packages.grpc.io/)
source. 
NOTE: To download the package please manually [download](https://packages.grpc.io/archive/2018/07/a3b54ef90841ec45fe5e28f54245b7944d0904f9-d24c85c7-32ed-4924-b9af-80e7a4aeb34d/index.xml) the .nupkg file into a local directory. Then add a nuget source that points to that directory (That can be done in [Visual Studio](https://docs.microsoft.com/en-us/nuget/tools/package-manager-ui#package-sources) or Visual Studio for Mac via "Configure nuget sources"). After that, nuget will also explore that directory when looking for packages.



BACKGROUND
-------------
The example project supports `Xamarin.Android` and `Xamarin.iOS`.

For this sample, we've already generated the server and client stubs from [helloworld.proto][].

PREREQUISITES
-------------

- The latest version Visual Studio for Mac or Visual Studio 2017 with Xamarin support installed.

BUILD
-------

- Open the `HelloworldXamarin.sln` in Visual Studio (or Visual Studio for Mac)
- Build the solution (Build -> Build All)

Try it!
-------

You can deploy the example apps directly through Xamarin Studio IDE.
Deployments can target both Android and iOS (both support physical device
deployment as well as simulator).

[helloworld.proto]:../../protos/helloworld.proto
