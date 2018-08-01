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
gRPC C# currently doesn't support Unity, but some proof-of-concept
work has been done. There is in-progress effort to provide users
with a pre-built gRPC package that can be used in their projects.
