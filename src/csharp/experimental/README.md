This directory contains useful resources for getting gRPC C# to work on
platforms that are not yet fully supported.

# Xamarin

gRPC C# now has experimental support for Xamarin.
See [HelloworldXamarin](/examples/csharp/HelloworldXamarin) for an example how to use it.

Starting from gRPC C# 2.34.x: in addition to the regular `Grpc.Core` dependency, you will also
need to add `Grpc.Core.Xamarin` dependency to your project (which has the mobile-specific builds of c# native extension library).
The `Grpc.Core` and `Grpc.Core.Xamarin` package versions must always match exactly for things to work.
Also note that the `Grpc.Core.Xamarin` needs to be added to your `*.Android` and `*.iOS` projects
in order for the native library bindings to be registered correctly (see https://github.com/grpc/grpc/issues/16250).

What's currently supported:

Xamarin.Android
- supported API level: Kitkat 4.4+ (= API level 19)
- supported ABIs: `armeabi-v7a` (vast majority of Android devices out there),
  `arm64-v8a` (some newer Android devices), `x86` (for emulator)

Xamarin.iOS
- supported architectures: armv7, arm64 (iPhone 6+) and x86_64 (iPhone simulator)

# Unity

gRPC C# now has experimental support for Unity. Please try using gRPC with
Unity and provide feedback!

How to test gRPC in a Unity project

1. Create a Unity project that targets .NET 4.x Equivalent (Edit -> Project Settings -> Player -> Configuration -> Scripting Runtime Version). gRPC uses APIs that are only available in .NET4.5+ so this is a requirement.

2. Download the latest development build of `grpc_unity_package.VERSION.zip` from
   [daily builds](https://packages.grpc.io/)

3. Extract the `.zip` file in the `Assets` directory in your Unity project

4. Unity IDE will pick up all the bundled files and add them to project automatically.
   You should be able to use gRPC and Protobuf in your scripts from now on.

5. (optional) Extra steps for iOS, see below

What's currently bundled in the `grpc_unity_package`
-  Grpc.Core and its dependencies
-  Google.Protobuf
-  Precompiled native libraries for Linux, MacOS, Windows, Android and iOS.

Please note that `Grpc.Core` is now in maintenance mode (see [The future of gRPC in C# belongs to grpc-dotnet](https://grpc.io/blog/grpc-csharp-future/)). There is a plan to support Unity in `Grpc.Net.Client`, which depends on Unity's .NET 5 or .NET 6 support. See [this issue](https://github.com/grpc/grpc-dotnet/issues/1309) for more information.

## Building for iOS

To build a Unity app on iOS, there are extra steps to do to make it work:

1. Add a `Assets/link.xml` asset file to your Unity project with the following content:
   ```xml
   <linker>
       <assembly fullname="UnityEngine">
          <type fullname="UnityEngine.Application" preserve="fields">
               <property name="platform"/>
           </type>
       </assembly>
   </linker>
   ```
   If you don't, you might encounter the following error: `System.IO.FileNotFoundException: Error loading native library. Not found in any of the possible locations:` with a list of paths that point to the `libgrpc_csharp_ext.x64.dylib` file.
2. Due to the growing build size, bitcode has been disabled for the gRPC library. You must disable it in your XCode project as well.
3. Add the `libz` framework.

Steps 2 and 3 can be automated by adding the following `Assets/Scripts/BuildIos.cs` script in your Unity project, and attaching it to a Unity game object:

```cs
#if UNITY_EDITOR && UNITY_IOS
using System.IO;
using UnityEngine;
using UnityEditor;
using UnityEditor.Callbacks;
using UnityEditor.iOS.Xcode;

public class BuildIos
{
  [PostProcessBuild]
  public static void OnPostProcessBuild(BuildTarget target, string path)
  {
    var projectPath = PBXProject.GetPBXProjectPath(path);
    var project = new PBXProject();
    project.ReadFromString(File.ReadAllText(projectPath));
#if UNITY_2019_3_OR_NEWER
    var targetGuid = project.GetUnityFrameworkTargetGuid();
#else
    var targetGuid = project.TargetGuidByName(PBXProject.GetUnityTargetName());
#endif

    // libz.tbd for grpc ios build
    project.AddFrameworkToProject(targetGuid, "libz.tbd", false);

    // bitode is disabled for libgrpc_csharp_ext, so need to disable it for the whole project
    project.SetBuildProperty(targetGuid, "ENABLE_BITCODE", "NO");

    File.WriteAllText(projectPath, project.WriteToString());
  }
}
#endif
```
