# Grpc.Tools - Protocol Buffers/gRPC C# Code Generation Build Integration

This package provides C# tooling support for generating C# code from `.proto` files in `.csproj` projects:
* It contains protocol buffers compiler and gRPC plugin to generate C# code.
* It can be used in building both grpc-dotnet projects and legacy c-core C# projects.

The package is used to automatically generate the C# code for protocol buffer messages
and gRPC service stubs from `.proto` files. These files:
* are generated on an as-needed basis each time the project is built.
* aren't added to the project or checked into source control.
* are a build artifact usually contained in the `obj` directory.

This package is optional. You may instead choose to generate the C# source files from
`.proto` files by running the `protoc` compiler manually or from a script.

## Simple example

To add a `.proto` file to a project edit the project’s `.csproj` file and add an item group with a `<Protobuf>` element that refers to the `.proto` file, e.g.

```xml
<ItemGroup>
    <Protobuf Include="Protos\greet.proto" />
</ItemGroup>
```

For more complex examples and detailed information on how to use this package see: [BUILD-INTEGRATION.md](https://github.com/grpc/grpc/blob/master/src/csharp/BUILD-INTEGRATION.md)
