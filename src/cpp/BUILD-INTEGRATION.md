Protocol Buffers/gRPC Codegen Integration Into C++ MSBuild
=================================================

The Grpc.Tools NuGet package offers [.NET MSBuild integration](../csharp/BUILD-INTEGRATION.md) 
and lists [C++ MSBuild integration](../csharp/BUILD-INTEGRATION.md#what-about-c-projects) 
as in the works. However, the NuGet package [does not contain the plugin](https://github.com/grpc/grpc/issues/19124)
necessary for C++ codegen.
Instead, the [vcpkg C++ package manager](https://vcpkg.io)
can be used to install an unofficial, community-maintainted gRPC package for C++ 
which can then be integrated into a C++ MSBuild project.

Install and integrate vcpkg and follow instructions for 
[MSBuild integration](https://vcpkg.io/en/docs/users/buildsystems/msbuild-integration.html).
The better outcome is to use the "Manifest" method to integrate packages per project, 
rather than user-wide integration, because the per-project tool paths will be placed in a project-relative path.

Add "grpc" as a dependency to the vcpkg manifest file (or for user-wide integration, install the package using the vcpkg command line)

In the C++ MSBuild project settings, create convenient properties to locate the installed tools 
(configure the paths as appropriate for your install):
```
<PropertyGroup>
  <VcpkgInstallRoot>$(MSBuildThisFileDirectory)vcpkg_installed\x64-windows\x64-windows\</VcpkgInstallRoot>
  <Protobuf_ProtocFullPath>$(VcpkgInstallRoot)tools\protobuf\protoc.exe</Protobuf_ProtocFullPath>
  <Protobuf_StandardImportsPath>$(VcpkgInstallRoot)include\</Protobuf_StandardImportsPath>
  <gRPC_PluginFullPath>$(VcpkgInstallRoot)tools\grpc\grpc_cpp_plugin.exe"</gRPC_PluginFullPath>
</PropertyGroup>
```
Set up a prebuild event to generate the C++ code:
```
<PreBuildEvent>
  <Message>Generate gRPC code</Message>
  <Command>
    "$(Protobuf_ProtocFullPath)" --cpp_out=.  ...path/to/protos/*.proto --proto_path=...path/to/protos/ --proto_path=$(Protobuf_StandardImportsPath)
  </Command>
  <Command>
    "$(Protobuf_ProtocFullPath)" --grpc_out=. ...path/to/protos/*.proto --proto_path=...path/to/protos/ --proto_path=$(Protobuf_StandardImportsPath) --plugin=protoc-gen-grpc=$(gRPC_PluginFullPath)
  </Command>
</PreBuildEvent>
```
The generated source files can be added to the project manually; or since the code 
is dynamically generated, it would be nice to also compile it dynamically.
C++ MSBuild does not by default support this, 
but can be made to partially [support dynamic inputs](https://docs.microsoft.com/en-us/cpp/build/reference/vcxproj-files-and-wildcards):
```
  <ItemGroup Label="Compile generated gRPC code">
    <_WildCardClCompile Include="*.pb.cc" />
    <ClCompile Include="@(_WildCardClCompile)" />
  </ItemGroup>
```
