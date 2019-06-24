### Cancelling RPCs

RPCs may be cancelled by both the client and the server.

#### Cancellation on the Client Side

A client may cancel an RPC for several reasons. Perhaps the data it requested
has been made irrelevant. Perhaps you, as the client, want to be a good citizen
of the server and are conserving compute resources.

#### Cancellation on the Server Side

A server is reponsible for cancellation in two ways. It must respond in some way
when a client initiates a cancellation, otherwise long-running computations
could continue indefinitely.

It may also decide to cancel the RPC for its own reasons. In our example, the
server can be configured to cancel an RPC after a certain number of hashes has
been computed in order to conserve compute resources.

##### Responding to Cancellations from a Servicer Thread

It's important to remember that a gRPC Python server is backed by a thread pool
with a fixed size. When an RPC is cancelled, the library does *not* terminate
your servicer thread. It is your responsibility as the application author to
ensure that your servicer thread terminates soon after the RPC has been
cancelled.

In this example, we use the `ServicerContext.add_callback` method to set a
`threading.Event` object when the RPC is terminated. We pass this `Event` object
down through our hashing algorithm and ensure to check that the RPC is still
ongoing before each iteration.


##### Initiating a Cancellation from a Servicer

Initiating a cancellation from the server side is simpler. Just call
`ServicerContext.cancel()`.
