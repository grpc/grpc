# gRPC Python Example for Wait-for-ready

The default behavior of an RPC is to fail instantly if the server is not ready yet. This example demonstrates how to change that behavior.


### Definition of 'wait-for-ready' semantics
> If an RPC is issued but the channel is in TRANSIENT_FAILURE or SHUTDOWN states, the RPC is unable to be transmitted promptly. By default, gRPC implementations SHOULD fail such RPCs immediately. This is known as "fail fast," but the usage of the term is historical. RPCs SHOULD NOT fail as a result of the channel being in other states (CONNECTING, READY, or IDLE).
> 
> gRPC implementations MAY provide a per-RPC option to not fail RPCs as a result of the channel being in TRANSIENT_FAILURE state. Instead, the implementation queues the RPCs until the channel is READY. This is known as "wait for ready." The RPCs SHOULD still fail before READY if there are unrelated reasons, such as the channel is SHUTDOWN or the RPC's deadline is reached.
> 
> From https://github.com/grpc/grpc/blob/master/doc/wait-for-ready.md 


### Use cases for 'wait-for-ready'

When developers spin up gRPC clients and servers at the same time, it is very like to fail first couple RPC calls due to unavailability of the server. If developers failed to prepare for this situation, the result can be catastrophic. But with 'wait-for-ready' semantics, developers can initialize the client and server in any order, especially useful in testing.

Also, developers may ensure the server is up before starting client. But in some cases like transient network failure may result in a temporary unavailability of the server. With 'wait-for-ready' semantics, those RPC calls will automatically wait until the server is ready to accept incoming requests.


### DEMO Snippets

```Python
# Per RPC level
stub = ...Stub(...)

stub.important_transaction_1(..., wait_for_ready=True)
stub.unimportant_transaction_2(...)
stub.important_transaction_3(..., wait_for_ready=True)
stub.unimportant_transaction_4(...)
# The unimportant transactions can be status report, or health check, etc.
```
