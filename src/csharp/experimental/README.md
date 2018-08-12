This directory contains useful resources for getting gRPC C# to work on
platforms that are not yet fully supported.

# Xamarin

gRPC C# now has experimental support for Xamarin.
See [HelloworldXamarin](/examples/csharp/HelloworldXamarin) for an example how to use it.

What's currently supported:

Xamarin.Android
- supported API level: Kitkat 4.4+ (= API level 19)
- supported ABIs: `armeabi-v7a` (vast majority of Android devices out there), 
  `arm64-v8a` (some newer Android devices), `x86` (for emulator)

Xamarin.iOS
- supported architectures: arm64 (iPhone 6+) and x86_64 (iPhone simulator)

# Unity

gRPC C# now has experimental support for Unity. Please try using gRPC with
Unity and provide feedback!

How to test gRPC in a Unity project
1. Create a Unity project that targets .NET 4.x (Edit -> Project Settings -> Editor -> Scripting Runtime Version). gRPC uses APIs that are only available in .NET4.5+ so this is a requirement.
2. Download the latest development build of `grpc_unity_package.VERSION.zip` from
   [daily builds](https://packages.grpc.io/)
3. Extract the `.zip` file in the `Assets` directory in your Unity project
4. Unity IDE will pick up all the bundled files and add them to project automatically.
   You should be able to use gRPC and Protobuf in your scripts from now on.

What's currently bundled in the `grpc_unity_package`
-  Grpc.Core and its dependencies
-  Google.Protobuf
-  Precompiled native libraries for Linux, MacOS, Windows, Android and iOS.
