# gRPC Python Async Interceptor Example

This example demonstrate the usage of Async interceptors and context propagation using [contextvars](https://docs.python.org/3/library/contextvars.html#module-contextvars).

## When to use contextvars

`Contextvars` can be used to propagate context in a same thread or coroutine, some example usage include:

1. Propagate from interceptor to another interceptor.
2. Propagate from interceptor to the server handler.
3. Propagate from client to server.

## How does this example works

This example have the following steps:
1. Generate RPC ID on client side and propagate to server using `metadata`.
    * `contextvars` can be used here if client and server is running in a same coroutine (or same thead for Sync).
2. Server interceptor1 intercept the request, it checks `rpc_id_var` and decorate it with it's tag `Interceptor1`.
3. Server interceptor2 intercept the request, it checks `rpc_id_var` and decorate it with it's tag `Interceptor2`.
4. Server handler receives the request with `rpc_id_var` decorated by both interceptor1 and interceptor2.

## How to run this example

1. Start server: `python3 -m async_greeter_server_with_interceptor`
2. Start client: `python3 -m async_greeter_client`

### Expected outcome

* On client side, you should see logs similar to:

```
Sending request with rpc id: 59ac966558b3d7d11a06bd45f1a0f89d
Greeter client received: Hello, you!
```

* On server side, you should see logs similar to:

```
INFO:root:Starting server on [::]:50051
INFO:root:Interceptor1 called with rpc_id: default
INFO:root:Interceptor2 called with rpc_id: Interceptor1-59ac966558b3d7d11a06bd45f1a0f89d
INFO:root:Handle rpc with id Interceptor2-Interceptor1-59ac966558b3d7d11a06bd45f1a0f89d in server handler.
```
