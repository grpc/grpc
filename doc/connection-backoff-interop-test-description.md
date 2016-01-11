Connection Backoff Interop Test Descriptions
===============================================

This test is to verify the client is reconnecting the server with correct
backoffs as specified in
[the spec](http://github.com/grpc/grpc/blob/master/doc/connection-backoff.md).
The test server has a port (control_port) running a rpc service for controlling
the server and another port (retry_port) to close any incoming tcp connections.
The test has the following flow:

1. The server starts listening on control_port.
2. The client calls Start rpc on server control_port.
3. The server starts listening on retry_port.
4. The client connects to server retry_port and retries with backoff for 540s,
which translates to about 13 retries.
5. The client calls Stop rpc on server control port.
6. The client checks the response to see whether the server thinks the backoffs
are conforming the spec or do its own check on the backoffs in the response.

Client and server use
[test.proto](https://github.com/grpc/grpc/blob/master/src/proto/grpc/testing/test.proto).
Each language should implement its own client. The C++ server is shared among
languages.

Client
------

Clients should accept these arguments:
* --server_control_port=PORT
    * The server port to connect to for rpc. For example, "8080"
* --server_retry_port=PORT
    * The server port to connect to for testing backoffs. For example, "8081"

The client must connect to the control port without TLS. The client must connect
to the retry port with TLS. The client should either assert on the server
returned backoff status or check the returned backoffs on its own.

Procedure of client:

1. Calls Start on server control port with a large deadline or no deadline,
waits for its finish and checks it succeeded.
2. Initiates a channel connection to server retry port, which should perform
reconnections with proper backoffs. A convienent way to achieve this is to
call Start with a deadline of 540s. The rpc should fail with deadline exceeded.
3. Calls Stop on server control port and checks it succeeded.
4. Checks the response to see whether the server thinks the backoffs passed the
   test.
5. Optionally, the client can do its own check on the returned backoffs.


Server
------

A C++ server can be used for the test. Other languages do NOT need to implement
a server. To minimize the network delay, the server binary should run on the
same machine or on a nearby machine (in terms of network distance) with the
client binary.

A server implements the ReconnectService to its state. It also opens a
tcp server on the retry_port, which just shuts down all incoming tcp
connections to simulate connection failures. The server will keep a record of
all the reconnection timestamps and return the connection backoffs in the
response in milliseconds. The server also checks the backoffs to see whether
they conform the spec and returns whether the client passes the test.

If the server receives a Start call when another client is being tested, it
finishes the call when the other client is done. If some other host connects
to the server retry_port when a client is being tested, the server will log an
error but likely would think the client fails the test.

The server accepts these arguments:

* --control_port=PORT
    * The port to listen on for control rpcs. For example, "8080"
* --retry_port=PORT
    * The tcp server port. For example, "8081"

