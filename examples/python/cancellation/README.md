### Cancelling RPCs

RPCs may be cancelled by both the client and the server.

#### Cancellation on the Client Side

A client may cancel an RPC for several reasons. Perhaps the data it requested
has been made irrelevant. Perhaps you, as the client, want to be a good citizen
of the server and are conserving compute resources.

##### Cancelling a Client-Side Unary RPC

The default RPC methods on a stub will simply return the result of an RPC.

```python
>>> stub = hash_name_pb2_grpc.HashFinderStub(channel)
>>> stub.Find(hash_name_pb2.HashNameRequest(desired_name=name))
<hash_name_pb2.HashNameResponse object at 0x7fe2eb8ce2d0>
```

But you may use the `future()` method to receive an instance of `grpc.Future`.
This interface allows you to wait on a response with a timeout, add a callback
to be executed when the RPC completes, or to cancel the RPC before it has
completed.

In the example, we use this interface to cancel our in-progress RPC when the
user interrupts the process with ctrl-c.

```python
stub = hash_name_pb2_grpc.HashFinderStub(channel)
future = stub.Find.future(hash_name_pb2.HashNameRequest(desired_name=name))
def cancel_request(unused_signum, unused_frame):
    future.cancel()
signal.signal(signal.SIGINT, cancel_request)
```

##### Cancelling a Client-Side Streaming RPC

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
