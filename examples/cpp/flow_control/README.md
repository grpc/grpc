gRPC Flow Control Example
=====================

# Overview

Flow control is relevant for streaming RPC calls.

The underlying layer will make the write wait when there is no space to write
the next message. This causes the request stream to go into a not ready state
and the method invocation waits.

# Server flow control

In server case, gRPC will pause the server implementation that is sending the
messages too fast. Server implementation is in
[server_flow_control_server.cc](server_flow_control_server.cc). It will write
a specified number of responses of a specified size as fast as possible.
As client-side buffer is filled, the write operation will block until the buffer
is freed.

A client implementation in [server_flow_control_client.cc](server_flow_control_client.cc)
will delay for 1s before starting a next read to simulate client that does not
have resources for handling the replies.

# Related information

Also see [gRPC Flow Control Users Guide][user guide]

 [user guide]: https://grpc.io/docs/guides/flow-control