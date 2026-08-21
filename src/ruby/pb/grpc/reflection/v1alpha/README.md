# gRPC Ruby Server Reflection

Enables tools like `grpcurl`, `grpc_cli`, and Postman to discover services and call RPCs without `.proto` files.

## Quick Start

```ruby
require 'grpc'
require 'grpc/reflection/v1alpha/server'

server = GRPC::RpcServer.new
server.handle(MyServiceImpl.new)
server.handle(
  Grpc::Reflection::V1alpha::Server.new(
    %w[my.package.MyService grpc.reflection.v1alpha.ServerReflection]
  )
)
server.run
```

Service names are passed explicitly — you control exactly which services are advertised.

## API

```ruby
Grpc::Reflection::V1alpha::Server.new(service_names, pool: nil)
```

- `service_names` — Array of fully-qualified service names
- `pool` — optional `DescriptorPool` (defaults to `generated_pool`)

Use `Grpc::Reflection::V1alpha::SERVICE_NAME` to include the reflection service itself.

## Supported Endpoints

| Endpoint | Description |
|----------|-------------|
| `list_services` | All registered service names |
| `file_by_filename` | `FileDescriptorProto` for a `.proto` file |
| `file_containing_symbol` | `FileDescriptorProto` containing a symbol |
| `file_containing_extension` | `FileDescriptorProto` containing an extension |
| `all_extension_numbers_of_type` | All extension numbers for a message type |

See the [v1alpha proto definition][proto] for full details.

[proto]: https://github.com/grpc/grpc/blob/master/src/proto/grpc/reflection/v1alpha/reflection.proto

## grpcurl Examples

```bash
grpcurl -plaintext localhost:50051 list
grpcurl -plaintext localhost:50051 describe my.package.MyService
grpcurl -plaintext -d '{"name":"world"}' localhost:50051 my.package.MyService/SayHello
```

## Notes

- Service names must be fully-qualified (e.g., `grpc.health.v1.Health`)
- All indexes are built at init — the servicer is thread-safe
