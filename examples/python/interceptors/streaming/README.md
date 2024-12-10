# gRPC Python Client/Server Streaming Interceptor Example

This example demonstrates the usage of streaming client/server interceptors.

## How to run this example

1. Start server: `python3 greeter_server.py`
1. Start client: `python3 greeter_client.py`

### Expected outcome

#### unary_stream

server

```
INFO:root:interceptor1: before RPC
INFO:root:interceptor2: before RPC
INFO:root:interceptor3: before RPC
INFO:root:SayHelloServerStreaming: received request 0   # ---------------
INFO:root:interceptor3: before response 0 returned      # |
INFO:root:interceptor2: before response 0 returned      # |
INFO:root:interceptor1: before response 0 returned      # | 1st response
INFO:root:interceptor1: after response 0 returned       # |
INFO:root:interceptor2: after response 0 returned       # |
INFO:root:interceptor3: after response 0 returned       # ---------------
INFO:root:SayHelloServerStreaming: received request 1   # ---------------
INFO:root:interceptor3: before response 1 returned      # |
INFO:root:interceptor2: before response 1 returned      # |
INFO:root:interceptor1: before response 1 returned      # | 2nd response
INFO:root:interceptor1: after response 1 returned       # |
INFO:root:interceptor2: after response 1 returned       # |
INFO:root:interceptor3: after response 1 returned       # ---------------
INFO:root:interceptor3: after RPC
INFO:root:interceptor2: after RPC
INFO:root:interceptor1: after RPC
```

client

```
INFO:root:interceptor1: before RPC
INFO:root:interceptor2: before RPC
INFO:root:interceptor3: before RPC
INFO:root:interceptor3: before response 0 returned      # ---------------
INFO:root:interceptor2: before response 0 returned      # |
INFO:root:interceptor1: before response 0 returned      # |
INFO:root:Hello number 0, Alice!                        # | 1st response
INFO:root:interceptor1: after response 0 returned       # |
INFO:root:interceptor2: after response 0 returned       # |
INFO:root:interceptor3: after response 0 returned       # ---------------
INFO:root:interceptor3: before response 1 returned      # ---------------
INFO:root:interceptor2: before response 1 returned      # |
INFO:root:interceptor1: before response 1 returned      # |
INFO:root:Hello number 1, Alice!                        # | 2nd response
INFO:root:interceptor1: after response 1 returned       # |
INFO:root:interceptor2: after response 1 returned       # |
INFO:root:interceptor3: after response 1 returned       # ---------------
INFO:root:interceptor3: after RPC
INFO:root:interceptor2: after RPC
INFO:root:interceptor1: after RPC
```
