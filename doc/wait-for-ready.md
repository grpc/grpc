gRPC Wait for Ready Semantics
=============================

If an RPC is issued but the channel is in `TRANSIENT_FAILURE` or `SHUTDOWN`
states, the RPC is unable to be transmitted promptly. By default, gRPC
implementations SHOULD fail such RPCs immediately. This is known as "fail fast,"
but usage of the term is historical. RPCs SHOULD NOT fail as a result of the
channel being in other states (`CONNECTING`, `READY`, or `IDLE`).

gRPC implementations MAY provide a per-RPC option to not fail RPCs as a result
of the channel being in `TRANSIENT_FAILURE` state. Instead, the implementation
queues the RPCs until the channel is `READY`. This is known as "wait for ready."
The RPCs SHOULD still fail before `READY` if there are unrelated reasons, such
as the channel is `SHUTDOWN` or the RPC's deadline is reached.
