# gRPC Python Server Reflection

This document shows how to use gRPC Server Reflection in gRPC Python.
Please see [C++ Server Reflection Tutorial] for general information
and more examples how to use server reflection.

## Enable server reflection in Python servers

gRPC Python Server Reflection is an add-on library. To use it, first install
the [grpcio-reflection] PyPI package into your project.

Note that with Python you need to manually register the service
descriptors with the reflection service implementation when creating a server
(this isn't necessary with e.g. C++ or Java)
```python
# add the following import statement to use server reflection
from grpc_reflection.v1alpha import reflection
# ...
def serve():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    helloworld_pb2_grpc.add_GreeterServicer_to_server(Greeter(), server)
    # the reflection service will be aware of "Greeter" and "ServerReflection" services.
    SERVICE_NAMES = (
        helloworld_pb2.DESCRIPTOR.services_by_name['Greeter'].full_name,
        reflection.SERVICE_NAME,
    )
    reflection.enable_server_reflection(SERVICE_NAMES, server)
    server.add_insecure_port('[::]:50051')
    server.start()
```

Please see [greeter_server_with_reflection.py] in the examples directory for the full
example, which extends the gRPC [Python `Greeter` example] on a reflection-enabled server.

After starting the server, you can verify that the server reflection
is working properly by using the [`grpc_cli` command line tool]:

 ```sh
  $ grpc_cli ls localhost:50051
  ```

  output:
  ```sh
  grpc.reflection.v1alpha.ServerReflection
  helloworld.Greeter
  ```

  For more examples and instructions how to use the `grpc_cli` tool,
  please refer to the [`grpc_cli` documentation] and the
  [C++ Server Reflection Tutorial].


## Use Server Reflection in a Python client

Server Reflection can be used by clients to get information about gRPC services
at runtime. We've provided a descriptor database called
[ProtoReflectionDescriptorDatabase](../../src/python/grpcio_reflection/v1alpha/proto_reflection_descriptor_database.h)
which implements the
[DescriptorDatabase](https://googleapis.dev/python/protobuf/latest/google/protobuf/descriptor_database.html#google.protobuf.descriptor_database.DescriptorDatabase)
interface. It manages the communication between clients and reflection services
and the storage of received information. Clients can use it as using a local
descriptor database.

- To use Server Reflection with ProtoReflectionDescriptorDatabase, first
  initialize an instance with a channel.

  ```Python
  import grpc
  from grpc_reflection.v1alpha.proto_reflection_descriptor_database import ProtoReflectionDescriptorDatabase

  channel = grpc.secure_channel(server_address, creds)
  reflection_db = ProtoReflectionDescriptorDatabase(channel)
  ```

- Then use this instance to feed a
  [DescriptorPool](https://googleapis.dev/python/protobuf/latest/google/protobuf/descriptor_pool.html#google.protobuf.descriptor_pool.DescriptorPool).

  ```Python
  from google.protobuf.descriptor_pool import DescriptorPool

  desc_pool = DescriptorPool(reflection_db)
  ```

- Example usage of this descriptor pool:

  * Get Service/method descriptors.

    ```Python
    service_desc = desc_pool.FindServiceByName("helloworld.Greeter")
    method_desc = service_desc.FindMethodByName("helloworld.Greeter.SayHello")
    ```

  * Get message type descriptors and create messages dynamically.

    ```Python
    request_desc = desc_pool.FindMessageTypeByName("helloworld.HelloRequest")
    request = MessageFactory(desc_pool).GetPrototype(request_desc)()
    ```

- You can also use the Reflection Database to list all the services:

    ```Python
    services = reflection_db.get_services()
    ```


## Additional Resources

The [Server Reflection Protocol] provides detailed
information about how the server reflection works and describes the server reflection
protocol in detail.


[C++ Server Reflection Tutorial]: ../server_reflection_tutorial.md
[grpcio-reflection]: https://pypi.org/project/grpcio-reflection/
[greeter_server_with_reflection.py]: https://github.com/grpc/grpc/blob/master/examples/python/helloworld/greeter_server_with_reflection.py
[Python `Greeter` example]: https://github.com/grpc/grpc/tree/master/examples/python/helloworld
[`grpc_cli` command line tool]: https://github.com/grpc/grpc/blob/master/doc/command_line_tool.md
[`grpc_cli` documentation]: ../command_line_tool.md
[C++ Server Reflection Tutorial]: ../server_reflection_tutorial.md
[Server Reflection Protocol]: ../server-reflection.md
