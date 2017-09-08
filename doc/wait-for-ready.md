# gRPC Wait for Ready Semantics

Errors can occur in many places. For the propose of this document we can bucket
the errors into three separate situations:

1.  The RPC never leaves the gRPC client library.
2.  The RPC goes onto the wire, but is **guaranteed** to not have been processed
    by the server application.
3.  The RPC reaches the server application, which returns a non-OK status.

![Where RPCs Fail](images/WhereRPCsFail.png)

In all of these cases, by default, gRPC implementations SHOULD immediately
return the error to the client application. This is known as "fail fast" but the
usage of the term is historical.

gRPC implementations MAY provide a per-RPC option to not fail RPCs as a result
of reason 1 and 2 (the cases in which the RPC never made it to the server
application). This is known as "wait for ready" behavior. An RPC with this
behavior enabled will stay pending on the client until either: 
  * it sees an *irrecoverable* failure 
  * its deadline is reached

**NOTE**: error case 2 needs one more point of clarification. gRPC
implementations that support "wait for ready" SHOULD be cautious when the
failure occurs in the gRPC server library. Transparently retrying indefinitely
in this case will increase wire traffic. In these cases implementations should
make a reasonable attempt to "wait for ready" (like retrying three times
immediately), then fail back to the client application.
