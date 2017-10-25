This tests the ability of gRPC server to rotate its SSL cert for use
in future channels with clients while not affecting any existing
channel.

For the credentials, we use two disjoint certificate hierarchies
`cert_hier_1` and `cert_hier_2`.

The test involves one server and two clients. Client 1 is a one-shot
client: establish the secure channel, perform the RPC, and exit;
client 2 establishes a secure channel, performs the RPC request, then
sleeps for a specified amount, then performs a second RPC request on
the same secure channel.

All three start out using `cert_hier_1` and we check that everything
works as expected, i.e., both client 1 and client 2 successfully
perform RPCs.

Then the server switches to `cert_hier_2`. Client 2, with its existing
secure channel to the server, must be able to perform another RPC
after the server's switch. Client 1 must fail while it continues to
use `cert_hier_1`, and must succeed when it switches to `cert_hier_2`.

All the "success" checks are based on the standard output/error of the
two clients.
