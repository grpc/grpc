# Running a test locally during development

To start a server during development:
1. Choose an available port number.
2. Start the server:
```
GRPC_VERBOSITY=DEBUG ibazel run --compilation_mode=dbg //test/cpp/interop:interop_server -- --port={port_number}
```
3. Start the client:
```
GRPC_VERBOSITY=DEBUG ibazel run --test_output=streamed //test/cpp/interop:interop_client -- --server_port={port_number} --test_case={test_case}
```
