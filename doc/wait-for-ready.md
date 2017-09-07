gRPC Wait for Ready Semantics
=============================

Errors can occur in many places. For the propose of this document we can bucket the errors into three separate situations:

1. The RPC never leaves the gRPC client library, for example:
    * The connection was broken and the channel is in a `TRANSIENT_FAILURE` state.
2. The RPC reaches the gRPC server library, and is rejected. For example:
    * The server is momentarily overloaded and sends a GOAWAY frame.
3. The RPC reaches the server application, and fails.

![Where RPCs Fail](images/WhereRPCsFail.png)

In all of these cases, by default, gRPC implementations SHOULD immediately return the error to the client application. This is known as "fail fast" but the usage of the term is historical.

gRPC implementations MAY provide a per-RPC option to not fail RPCs as a result of reason 1 and 2 (the cases in which the RPC never made it to the server application). This is known as "wait for ready" behavior. An RPC with this behavior enabled will stay pending on the client until:
  * it sees an *irrecoverable* failure
  * its deadline is reached

Recalling the examples at top:
  * an overloaded server may recover
  * a channel in `TRANSIENT_FAILURE` may become connected

So we do not consider these types of failures *irrecoverable*.

**NOTE**: error case 2 needs one more point of clarification. gRPC implementations that support "wait for ready" MUST be cautious to protect the server when the failure occurs in the gRPC server library. Transparently retrying indefinitely in this case will burden the server and increase wire traffic. In these cases implementations should make a reasonable attempt to "wait for ready" (like retrying with backoff three times), then fail back to the client application.
