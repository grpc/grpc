### Cancellation

In the example, we implement a silly algorithm. We search for bytestrings whose
hashes are similar to a given search string. For example, say we're looking for
the string "doctor". Our algorithm may return `JrqhZVkTDoctYrUlXDbL6pfYQHU=` or
`RC9/7mlM3ldy4TdoctOc6WzYbO4=`. This is a brute force algorithm, so the server
performing the search must be conscious of the resources it allows to each client
and each client must be conscientious of the resources it demands of the server.

In particular, we ensure that client processes cancel the stream explicitly
before terminating and we ensure that server processes cancel RPCs that have gone on longer
than a certain number of iterations.

#### Cancellation on the Client Side

A client may cancel an RPC for several reasons. Perhaps the data it requested
has been made irrelevant. Perhaps you, as the client, want to be a good citizen
of the server and are conserving compute resources.

##### Cancelling a Server-Side Unary RPC from the Client

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
    sys.exit(0)
signal.signal(signal.SIGINT, cancel_request)

result = future.result()
print(result)
```

We also call `sys.exit(0)` to terminate the process. If we do not do this, then
`future.result()` with throw an `RpcError`. Alternatively, you may catch this
exception.


##### Cancelling a Server-Side Streaming RPC from the Client

Cancelling a Server-side streaming RPC is even simpler from the perspective of
the gRPC API. The default stub method is already an instance of `grpc.Future`,
so the methods outlined above still apply. It is also a generator, so we may
iterate over it to yield the results of our RPC.

```python
stub = hash_name_pb2_grpc.HashFinderStub(channel)
result_generator = stub.FindRange(hash_name_pb2.HashNameRequest(desired_name=name))
def cancel_request(unused_signum, unused_frame):
    result_generator.cancel()
    sys.exit(0)
signal.signal(signal.SIGINT, cancel_request)
for result in result_generator:
    print(result)
```

We also call `sys.exit(0)` here to terminate the process. Alternatively, you may
catch the `RpcError` raised by the for loop upon cancellation.


#### Cancellation on the Server Side

A server is responsible for cancellation in two ways. It must respond in some way
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

```python
stop_event = threading.Event()
def on_rpc_done():
    # Regain servicer thread.
    stop_event.set()
context.add_callback(on_rpc_done)
secret = _find_secret(stop_event)
```

##### Initiating a Cancellation on the Server Side

Initiating a cancellation from the server side is simpler. Just call
`ServicerContext.cancel()`.

In our example, we ensure that no single client is monopolizing the server by
cancelling after a configurable number of hashes have been checked.

```python
try:
    for candidate in secret_generator:
        yield candidate
except ResourceLimitExceededError:
    print("Cancelling RPC due to exhausted resources.")
    context.cancel()
```

In this type of situation, you may also consider returning a more specific error
using the [`grpcio-status`](https://pypi.org/project/grpcio-status/) package.
