# gRPC Server Reflection Tutorial

gRPC Server Reflection provides information about publicly-accessible gRPC
services on a server, and assists clients at runtime to construct RPC
requests and responses without precompiled service information. It is used by
gRPC CLI, which can be used to introspect server protos and send/receive test
RPCs.

## Enable Server Reflection

### Enable server reflection in C++ servers

C++ Server Reflection is an add-on library, `libgrpc++_reflction`. To enable C++
server reflection, you can link this library to your server binary.

Some platforms (e.g. Ubuntu 11.10 onwards) only link in libraries that directly
contain symbols used by the application. On these platforms, LD flag
`--no-as-needed` is needed for for dynamic linking and `--whole-archive` is
needed for for static linking.

This [Makefile](../examples/cpp/helloworld/Makefile#L37#L45) demonstrates
enabling c++ server reflection on Linux and MacOS.

## Test services using Server Reflection

After enabling Server Reflection in a server application, you can use gRPC CLI
to test its services.

Instructions on how to use gRPC CLI can be found at
[command_line_tool.md](command_line_tool.md), or using `grpc_cli help` command.

Here we use `examples/cpp/helloworld` as an example to show the use of gRPC
Server Reflection and gRPC CLI. First, we need to build gRPC CLI and setup an
example server with Server Reflection enabled.

- Setup an example server

  Server Reflection has already been enabled in the
  [Makefile](../examples/cpp/helloworld/Makefile) of the helloworld example. We
  can simply make it and run the greeter_server.

  ```sh
  $ make -C examples/cpp/helloworld
  $ examples/cpp/helloworld/greeter_server &
  ```

- Build gRPC CLI

  ```sh
  make grpc_cli
  cd bins/opt
  ```

  gRPC CLI binary `grpc_cli` can be found at `bins/opt/` folder. This tool is
  still new and does not have a `make install` target yet.

### List services

`grpc_cli ls` command lists services and methods exposed at a given port

- List all the services exposed at a given port

  ```sh
  $ grpc_cli ls localhost:50051
  ```

  output:
  ```sh
  helloworld.Greeter
  grpc.reflection.v1alpha.ServerReflection
  ```

- List one service with details

  `grpc_cli ls` command inspects a service given its full name (in the format of
  \<package\>.\<service\>). It can print information with a long listing format
  when `-l` flag is set. This flag can be used to get more details about a
  service.

  ```sh
  $ grpc_cli ls localhost:50051 helloworld.Greeter -l
  ```

  output:
  ```sh
  filename: helloworld.proto
  package: helloworld;
  service Greeter {
    rpc SayHello(helloworld.HelloRequest) returns (helloworld.HelloReply) {}
  }

  ```

### List methods

- List one method with details

  `grpc_cli ls` command also inspects a method given its full name (in the
  format of \<package\>.\<service\>.\<method\>).

  ```sh
  $ grpc_cli ls localhost:50051 helloworld.Greeter.SayHello -l
  ```

  output:
  ```sh
    rpc SayHello(helloworld.HelloRequest) returns (helloworld.HelloReply) {}
  ```

### Inspect message types

We can use`grpc_cli type` command to inspect request/response types given the
full name of the type (in the format of \<package\>.\<type\>).

- Get information about the request type

  ```sh
  $ grpc_cli type localhost:50051 helloworld.HelloRequest
  ```

  output:
  ```sh
  message HelloRequest {
    optional string name = 1;
  }
  ```

### Call a remote method

We can send RPCs to a server and get responses using `grpc_cli call` command.

- Call a unary method

  ```sh
  $ grpc_cli call localhost:50051 SayHello "name: 'gRPC CLI'"
  ```

  output:
  ```sh
  message: "Hello gRPC CLI"
  ```

## Use Server Reflection in a C++ client

Server Reflection can be used by clients to get information about gRPC services
at runtime. We've provided a descriptor database called
[grpc::ProtoReflectionDescriptorDatabase](../test/cpp/util/proto_reflection_descriptor_database.h)
which implements the
[google::protobuf::DescriptorDatabase](https://developers.google.com/protocol-buffers/docs/reference/cpp/google.protobuf.descriptor_database#DescriptorDatabase)
interface. It manages the communication between clients and reflection services
and the storage of received information. Clients can use it as using a local
descriptor database.

- To use Server Reflection with grpc::ProtoReflectionDescriptorDatabase, first
  initialize an instance with a grpc::Channel.

  ```c++
  std::shared_ptr<grpc::Channel> channel =
      grpc::CreateChannel(server_address, server_cred);
  grpc::ProtoReflectionDescriptorDatabase reflection_db(channel);
  ```

- Then use this instance to feed a
  [google::protobuf::DescriptorPool](https://developers.google.com/protocol-buffers/docs/reference/cpp/google.protobuf.descriptor#DescriptorPool).

  ```c++
  google::protobuf::DescriptorPool desc_pool(&reflection_db);
  ```

- Example usage of this descriptor pool

  * Get Service/method descriptors.

    ```c++
    const google::protobuf::ServiceDescriptor* service_desc =
        desc_pool->FindServiceByName("helloworld.Greeter");
    const google::protobuf::MethodDescriptor* method_desc =
        desc_pool->FindMethodByName("helloworld.Greeter.SayHello");
    ```

  * Get message type descriptors.

    ```c++
    const google::protobuf::Descriptor* request_desc =
        desc_pool->FindMessageTypeByName("helloworld.HelloRequest");
    ```

  * Feed [google::protobuf::DynamicMessageFactory](https://developers.google.com/protocol-buffers/docs/reference/cpp/google.protobuf.dynamic_message#DynamicMessageFactory).

    ```c++
    google::protobuf::DynamicMessageFactory(&desc_pool);
    ```
