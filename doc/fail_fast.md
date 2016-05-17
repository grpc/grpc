gRPC Fail Fast Semantics
========================

Fail fast requests allow terminating requests (with status UNAVAILABLE) prior
to the deadline of the request being met.

gRPC implementations of fail fast can terminate requests whenever a channel is
in the TRANSIENT_FAILURE or SHUTDOWN states. If the channel is in any other
state (CONNECTING, READY, or IDLE) the request should not be terminated.

Fail fast SHOULD be the default for gRPC implementations, with an option to
switch to non fail fast.

The opposite of fail fast is 'ignore connectivity'.

