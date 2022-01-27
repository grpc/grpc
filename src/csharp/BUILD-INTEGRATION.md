Protocol Buffers/gRPC Codegen Integration Into .NET Build
=================================================

With Grpc.Tools package version 1.17 we made it easier to compile .proto files
in your project using the `dotnet build` command, Visual Studio, or command-line
MSBuild. You need to configure the .csproj project according to the way you want
to integrate Protocol Buffer files into your build.

There is also a Reference section at the end of the file.

Common scenarios
----------------

### I just want to compile .proto files into my library

This is the approach taken by the examples in the `csharp/examples` directory.
Protoc output files (for example, `Helloworld.cs` and `HelloworldGrpc.cs`
compiled from `helloworld.proto`) are placed among *object* and other temporary
files of your project, and automatically provided as inputs to the C# compiler.
As with other automatically generated .cs files, they are included in the source
and symbols NuGet package, if you build one.

Simply reference your .proto files in a `<Protobuf>` item group. The following
example will add all .proto files in a project and all its subdirectories
(excluding special directories such as `bin` and `obj`):

```xml
  <ItemGroup>
    <Protobuf Include="**/*.proto" />
  </ItemGroup>
```

You must add a reference to the NuGet packages Grpc.Tools and Grpc (the latter
is a meta-package, in turn referencing Grpc.Core and Google.Protobuf packages).
It is **very important** to mark Grpc.Tools as a development-only dependency, so
that the *users* of your library do not fetch the tools package:

* "dotnet SDK" .csproj (Visual Studio, `dotnet new`): Add an attribute
 `PrivateAssets="All"` to the Grpc.Tools package reference. See an example in the
 [Greeter.csproj](../../examples/csharp/Helloworld/Greeter/Greeter.csproj#L10)
 example project in this repository. If adding a package reference in Visual
 Studio, edit the project file and add this attribute. [This is a bug in NuGet
 client](https://github.com/NuGet/Home/issues/4125).

 * "Classic" .csproj with `packages.config` (Visual Studio, Mono): This is
 handled automatically by NuGet. See the attribute added by Visual Studio to the
 [packages.config](../../examples/csharp/HelloworldLegacyCsproj/Greeter/packages.config#L6)
 file in the HelloworldLegacyCsproj/Greeter example.

If building a NuGet package from your library with the nuget command line tool
from a .nuspec file, then the spec file may (and probably should) reference the
Grpc metapackage, but **do not add a reference to Grpc.Tools** to it. "dotnet SDK"
projects handle this automatically when called from `dotnet pack` by excluding
any packages with private assets, such as thus marked Grpc.Tools.

#### Per-file options that can be set in Visual Studio

For a "dotnet SDK" project, you have more control of some frequently used options.
**You may need to open and close Visual Studio** for this form to appear in the
properties window after adding a reference to Grpc.Tools package (we do not know
whether this is a bug or by design, but it looks like a bug):

![Properties in an SDK project](doc/integration.md-fig.2-sdk.png)

You can also change options of multiple files at once by selecting them in the
Project Explorer together.

For a "classic" project, you can only add .proto files with all options set to
default (if you find it necessary to modify these options, then hand-edit the
.csproj file). Click on the "show all files" button, add files to project, then
change file type of the .proto files to "Protobuf" in the Properties window
drop-down. This menu item will appear after you import the Grpc.Tools package:

![Properties in a classic project](doc/integration.md-fig.1-classic.png)

See the Reference section at end of this file for options that can be set
per-file by modifying the source .csproj directly.

#### My .proto files are in a directory outside the project

Refer to the example files
[RouteGuide.csproj](../../examples/csharp/RouteGuide/RouteGuide/RouteGuide.csproj#L58-L60)
and [Greeter.csproj](../../examples/csharp/Helloworld/Greeter/Greeter.csproj#L11)
in this repository. For the files to show up in Visual Studio properly, add a
`Link` attribute with just a filename to the `<Protobuf>` item. This will be the
display name of the file. In the `Include` attribute, specify the complete path
to file. A relative path is based off the project directory.

Or, if using Visual Studio, add files _as links_ from outside directory. In the
Add Files dialog, there is a little [down arrow near the Open
button](https://stackoverflow.com/a/9770061). Click on it, and choose "Add as
link". If you do not select this option, Visual Studio will copy files to the
project directory instead.

#### My .proto files have same filename in different folders

Starting from Grpc.Tools version 2.31, protocol buffers compilation preserves original folder structure for generated files. Eg.

- `../ProjectFolder/Protos/v2/http.proto`
- `../ProjectFolder/Protos/v3/http.proto`

Will result in:

- `../ProjectFolder/obj/CONFIGURATION/FRAMEWORK/Protos/v2/Greet.cs`
- `../ProjectFolder/obj/CONFIGURATION/FRAMEWORK/Protos/v2/GreetGrpc.cs`
- `../ProjectFolder/obj/CONFIGURATION/FRAMEWORK/Protos/v3/Greet.cs`
- `../ProjectFolder/obj/CONFIGURATION/FRAMEWORK/Protos/v3/GreetGrpc.cs`

This feature resolves problems we have faced in large projects. Moreover, There is now also a project-wide new option Protobuf_ProtoRoot to define the fallback ProtoRoot. If the ProtoRoot is set, this also reduces the amount of problems that lead to duplicates. Eg.

```xml
  <ItemGroup>
    <Protobuf Include="Protos\v2\greet.proto" ProtoRoot="Protos" />
  </ItemGroup>
```

Before Grpc.Tools version 2.31 all .proto files were compiled into `obj` directory, flattening relative paths. For proto files with duplicated names it cause following errors `NETSDK1022 Duplicate 'Compile' items were included. [...]` or `MSB3105 [...] Duplicate items are not supported by the "Sources" parameter`. The workaround for this problem was introducing relative paths in your `obj` folder, by manipulating output path. Eg. 

```xml
  <ItemGroup>
    <Protobuf Include="Protos/v2/http.proto" OutputDir="$(Protobuf_OutputPath)%(RelativeDir)"  />
    <Protobuf Include="Protos/v3/http.proto" OutputDir="$(Protobuf_OutputPath)%(RelativeDir)"  />
  </ItemGroup>
```

__Note, this was a workaround approach, we recommend updating Grpc.Tools to the latest version.__

### I just want to generate proto and gRPC C# sources from my .proto files (no C# compile)

Suppose you want to place generated files right beside each respective source
.proto file. Create a .csproj library file in the common root of your .proto
tree, and add a reference to Grpc.Tools package (this works in Windows too, `$`
below stands for a command prompt in either platform):

```
/myproject/myprotofiles$ dotnet new classlib
  . . .
  Restoring packages for /myproject/myprotofiles/myprotofiles.csproj...
  . . .
/myproject/myprotofiles$ rm *.cs        <-- remove all *.cs files from template;
C:\myproject\myprotofiles> del *.cs /y  <-- on Windows, use the del command instead.
/myproject/myprotofiles$ dotnet add package Grpc.Tools
```

(the latter command also accepts an optional `--version X.Y` switch for a
specific version of package, should you need one). Next open the generated
.csproj file in a text editor.

Since you are not building a package, you may not worry about adding
`PrivateAssets="All"` attribute, but it will not hurt, in case you are
repurposing the project at some time later. The important part is (1) tell the
gRPC tools to select the whole directory of files; (2) order placement of each
output besides its source, and (3) not compile the generated .cs files. Add the
following stanza under the `<Project>` xml node:

```xml
  <ItemGroup>
    <Protobuf Include="**/*.proto" OutputDir="%(RelativeDir)" CompileOutputs="false"  />
  </ItemGroup>
```

The `Include` tells the build system to recursively examine project directory
and its subdirectories (`**`) include all files matching the wildcard `*.proto`.
You can instead selectively include your files or selectively exclude files from
the glob pattern; [MSBuild documentation explains
that](https://docs.microsoft.com/visualstudio/msbuild/msbuild-items). The
`OutputDir="%(RelativeDir)"` orders the output directory for each .cs file be
same as the corresponding .proto directory. Finally, `CompileOutputs="false"`
prevents compiling the generated files into an assembly.

Note that an empty assembly is still generated, but you should ignore it. As
with any build system, it is used to detect out-of-date dependencies and
recompile them.

#### I am getting a warning about a missing expected file!

When we are preparing compile, there is no way to know whether a given proto
file will produce a *Grpc.cs output or not. If the proto file has a `service`
clause, it will; otherwise, it won't, but the build script cannot know that in
advance. When we are treating generated .cs files as temporary, this is ok, but
when generating them for you, creating empty files is probably not. You need to
tell the compiler which files should be compiled with gRPC services, and which
only contain protobuffer message definitions.

One option is just ignore the warning. Another is quench it by setting the
property `Protobuf_NoWarnMissingExpected` to `true`:

```xml
<PropertyGroup>
  <Protobuf_NoWarnMissingExpected>true</Protobuf_NoWarnMissingExpected>
</PropertyGroup>
```

For a small to medium projects this is sufficient. But because of a missing
output dependency, the corresponding .proto file will be recompiled on every
build. If your project is large, or if other large builds depend on generated
files, and are also needlessly recompiled, you'll want to prevent these rebuilds
when files have not in fact changed, as follows:

##### Explicitly tell protoc for which files it should use the gRPC plugin

You need to set the `Protobuf` item property `GrpcServices` to `None` for those
.proto inputs which do not have a `service` declared (or, optionally, those
which do but you do not want a service/client stub for). The default value for
the `GrpcServices` is `Both` (both client and server stub are generated). This
is easy enough to do with glob patterns if your files are laid out in
directories according to their service use, for example:

```xml
  <ItemGroup>
    <Protobuf Include="**/*.proto" OutputDir="%(RelativeDir)"
              CompileOutputs="false" GrpcServices="None" />
    <Protobuf Update="**/hello/*.proto;**/bye/*.proto" GrpcServices="Both" />
  </ItemGroup>
```

In this sample, all .proto files are compiled with `GrpcServices="None"`, except
for .proto files in subdirectories on any tree level named `hello/` and `bye`,
which will take `GrpcServices="Both"` Note the use of the `Update` attribute
instead of `Include`. If you write `Include` by mistake, the files will be added
to compile *twice*, once with, and once without GrpcServices. Pay attention not
to do that!

Another example would be the use of globbing if your service .proto files are
named according to a pattern, for example `*_services.proto`. In this case, The
`Update` attribute can be written as `Update="**/*_service.proto"`, to set the
attribute `GrpcServices="Both"` only on these files.

But what if no patterns work, and you cannot sort a large set of .proto file
into those containing a service and those not? As a last resort,

##### Force creating empty .cs files for missing outputs.

Naturally, this results in a dirtier compiler output tree, but you may clean it
using other ways (for example, by not copying zero-length .cs files to their
final destination). Remember, though, that the files are still important to keep
in their output locations to prevent needless recompilation. You may force
generating empty files by setting the property `Protobuf_TouchMissingExpected`
to `true`:

```xml
  <PropertyGroup>
    <Protobuf_TouchMissingExpected>true</Protobuf_TouchMissingExpected>
  </PropertyGroup>
```

#### But I do not use gRPC at all, I need only protobuffer messages compiled

Set `GrpcServices="None"` on all proto files:

```xml
  <ItemGroup>
    <Protobuf Include="**/*.proto" OutputDir="%(RelativeDir)"
              CompileOutputs="false" GrpcServices="None" />
  </ItemGroup>
```

#### That's good so far, but I do not want the `bin` and `obj` directories in my tree

You may create the project in a subdirectory of the root of your files, such as,
for example, `.build`. In this case, you want to refer to the proto files
relative to that `.build/` directory as

```xml
  <ItemGroup>
    <Protobuf Include="../**/*.proto" ProtoRoot=".."
              OutputDir="%(RelativeDir)" CompileOutputs="false" />
  </ItemGroup>
```

Pay attention to the `ProtoRoot` property. It needs to be set to the directory
where `import` declarations in the .proto files are looking for files, since the
project root is no longer the same as the proto root.

Alternatively, you may place the project in a directory *above* your proto root,
and refer to the files with a subdirectory name:

```xml
  <ItemGroup>
    <Protobuf Include="proto_root/**/*.proto" ProtoRoot="proto_root"
              OutputDir="%(RelativeDir)" CompileOutputs="false" />
  </ItemGroup>
```

### Alas, this all is nice, but my scenario is more complex, -OR-
### I'll investigate that when I have time. I just want to run protoc as I did before.

One option is examine our [.targets and .props files](Grpc.Tools/build/) and see
if you can create your own build sequence from the provided targets so that it
fits your needs. Also please open an issue (and tag @kkm000 in it!) with your
scenario. We'll try to support it if it appears general enough.

But if you just want to run `protoc` using MsBuild `<Exec>` task, as you
probably did before the version 1.17 of Grpc.Tools, we have a few build
variables that point to resolved names of tools and common protoc imports.
You'll have to roll your own dependency checking (or go with a full
recompilation each time, if that works for you), but at the very least each
version of the Tools package will point to the correct location of the files,
and resolve the compiler and plugin executables appropriate for the host system.
These property variables are:

* `Protobuf_ProtocFullPath` points to the full path and filename of protoc executable, e. g.,
  "C:\Users\kkm\.nuget\packages\grpc.tools\1.17.0\build\native\bin\windows\protoc.exe".

* `gRPC_PluginFullPath` points to the full path and filename of gRPC plugin, such as
  "C:\Users\kkm\.nuget\packages\grpc.tools\1.17.0\build\native\bin\windows\grpc_csharp_plugin.exe"

* `Protobuf_StandardImportsPath` points to the standard proto import directory, for example,
  "C:\Users\kkm\.nuget\packages\grpc.tools\1.17.0\build\native\include". This is
  the directory where a declaration such as `import "google/protobuf/wrappers.proto";`
  in a proto file would find its target.

Use MSBuild property expansion syntax `$(VariableName)` in your protoc command
line to substitute these variables, for instance,

```xml
  <Target Name="MyProtoCompile">
    <PropertyGroup>
      <ProtoCCommand>$(Protobuf_ProtocFullPath) --plugin=protoc-gen-grpc=$(gRPC_PluginFullPath)  -I $(Protobuf_StandardImportsPath) ....rest of your command.... </ProtoCCommand>
    </PropertyGroup>
    <Message Importance="high" Text="$(ProtoCCommand)" />
    <Exec Command="$(ProtoCCommand)" />
  </Target>
```

Also make sure *not* to include any file names to the `Protobuf` item
collection, otherwise they will be compiled by default. If, by any chance, you
used that name for your build scripting, you must rename it.

### What about C++ projects?

This is in the works. Currently, the same variables as above are set to point to
the protoc binary, C++ gRPC plugin and the standard imports, but nothing else.
Do not use the `Protobuf` item collection name so that your project remains
future-proof. We'll use it for C++ projects too.

Reference
---------

### Protobuf item metadata reference

The following metadata are recognized on the `<Protobuf>` items.

| Name           | Default   | Value                | Synopsis                         |
|----------------|-----------|----------------------|----------------------------------|
| Access         | `public`  | `public`, `internal`               | Generated class access           |
| AdditionalProtocArguments | | arbitrary cmdline arguments | Extra command line flags passed to `protoc` command |
| ProtoCompile   | `true`    | `true`, `false`                    | Pass files to protoc?            |
| ProtoRoot      | See notes | A directory                        | Common root for set of files     |
| CompileOutputs | `true`    | `true`, `false`                    | C#-compile generated files?      |
| OutputDir      | See notes | A directory                        | Directory for generated C# files with protobuf messages |
| OutputOptions  | | arbitrary options                  | Extra options passed to C# codegen as `--csharp_opt=opt1,opt2` |
| GrpcOutputDir  | See notes | A directory                        | Directory for generated gRPC stubs    |
| GrpcOutputOptions | | arbitrary options                  | Extra options passed to gRPC codegen as `--grpc_opt=opt1,opt2` |
| GrpcServices   | `both`    | `none`, `client`, `server`, `both` | Generated gRPC stubs             |

__Notes__

* __ProtoRoot__  
For files _inside_ the project cone, `ProtoRoot` is set by default to the
project directory. For every file _outside_ of the project directory, the value
is set to this file's containing directory name, individually per file. If you
include a subtree of proto files that lies outside of the project directory, you
need to set this metadatum. There is an example in this file above. The path in
this variable is relative to the project directory.

* __OutputDir__  
The default value for this metadatum is the value of the property
`Protobuf_OutputPath`. This property, in turn, unless you set it in your
project, will be set to the value of the standard MSBuild property
`IntermediateOutputPath`, which points to the location of compilation object
outputs, such as "obj/Release/netstandard1.5/". The path in this property is
considered relative to the project directory.

* __GrpcOutputDir__  
Unless explicitly set, will follow `OutputDir` for any given file.

* __Access__  
Sets generated class access on _both_ generated message and gRPC stub classes.

* __AdditionalProtocArguments__ 
Pass additional commandline arguments to the `protoc` command being invoked.
Normally this option should not be used, but it exists for scenarios when you need to pass
otherwise unsupported (e.g. experimental) flags to protocol buffer compiler.

* __GrpcOutputOptions__ 
Pass additional options to the `grpc_csharp_plugin` in form of the `--grpc_opt` flag.
Normally this option should not be used as it's values are already controlled by "Access"
and "GrpcServices" metadata, but it might be useful in situations where you want
to explicitly pass some otherwise unsupported (e.g. experimental) options to the
`grpc_csharp_plugin`.

`grpc_csharp_plugin` command line options
---------

Under the hood, the `Grpc.Tools` build integration invokes the `protoc` and `grpc_csharp_plugin` binaries
to perform code generation. Here is an overview of the available `grpc_csharp_plugin` options:

| Name            | Default   | Synopsis                                                 |
|---------------- |-----------|----------------------------------------------------------|
| no_client       | off       | Don't generate the client stub                           |
| no_server       | off       | Don't generate the server-side stub                      |
| internal_access | off       | Generate classes with "internal" visibility              |

Note that the protocol buffer compiler has a special commandline syntax for plugin options.
Example:
```
protoc --plugin=protoc-gen-grpc=grpc_csharp_plugin --csharp_out=OUT_DIR \
    --grpc_out=OUT_DIR --grpc_opt=lite_client,no_server \
    -I INCLUDE_DIR foo.proto
```
