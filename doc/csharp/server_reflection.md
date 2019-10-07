# gRPC C# Server Reflection

This document shows how to use gRPC Server Reflection in gRPC C#.
Please see [C++ Server Reflection Tutorial](../server_reflection_tutorial.md)
for general information and more examples how to use server reflection.

## Enable server reflection in C# servers

C# Server Reflection is an add-on library.
To use it, first install the [Grpc.Reflection](https://www.nuget.org/packages/Grpc.Reflection/)
Nuget package into your project.

Note that with C# you need to manually register the service
descriptors with the reflection service implementation when creating a server
(this isn't necessary with e.g. C++ or Java)
```csharp
// the reflection service will be aware of "Greeter" and "ServerReflection" services.
var reflectionServiceImpl = new ReflectionServiceImpl(Greeter.Descriptor, ServerReflection.Descriptor);
server = new Server()
{
    Services =
    {
        // the server will serve 2 services, the Greeter and the ServerReflection
        ServerReflection.BindService(new GreeterImpl()),
        ServerReflection.BindService(reflectionServiceImpl)
    },
    Ports = { { "localhost", 50051, ServerCredentials.Insecure } }
};
server.Start();
```
### Server Reflection in C# in .NET Core 3.0 sevices
Since now a days in .NET Core 3.0, sevices are registered in startup.cs and build in program.cs , below is the way one can implement Server Reflection

```csharp
//Create a class named ReflectionImplementation which inherits ReflectionServiceImpl (using nuget Grpc.Reflection)
 public class ReflectionImplementation: ReflectionServiceImpl
{
    public ReflectionImplementation():base(Calculator.Descriptor, Greeter.Descriptor, ServerReflection.Descriptor)
    {
    }

}
```

```csharp
// add below line startup.cs
endpoints.MapGrpcService<ReflectionImplementation>();
```


After starting the server, you can verify that the server reflection
is working properly by using the [`grpc_cli` command line
tool](https://github.com/grpc/grpc/blob/master/doc/command_line_tool.md):

 ```sh
  $ grpc_cli ls localhost:50051
  ```

  output:
  ```sh
  helloworld.Greeter
  grpc.reflection.v1alpha.ServerReflection
  ```

  For more examples and instructions how to use the `grpc_cli` tool,
  please refer to the [`grpc_cli` documentation](../command_line_tool.md)
  and the [C++ Server Reflection Tutorial](../server_reflection_tutorial.md).

## Additional Resources

The [Server Reflection Protocol](../server-reflection.md) provides detailed
information about how the server reflection works and describes the server reflection
protocol in detail.
