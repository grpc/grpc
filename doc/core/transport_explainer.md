# Transport Explainer

@vjpai

## Existing Transports

[gRPC
transports](https://github.com/grpc/grpc/tree/master/src/core/ext/transport)
plug in below the core API (one level below the C++ or other wrapped-language
API). You can write your transport in C or C++ though; currently (Nov 2017) all
the transports are nominally written in C++ though they are idiomatically C. The
existing transports are:

* [HTTP/2](https://github.com/grpc/grpc/tree/master/src/core/ext/transport/chttp2)
* [Cronet](https://github.com/grpc/grpc/tree/master/src/core/ext/transport/cronet)
* [In-process](https://github.com/grpc/grpc/tree/master/src/core/ext/transport/inproc)

Among these, the in-process is likely the easiest to understand, though arguably
also the least similar to a "real" sockets-based transport since it is only used
in a single process.

## Transport stream ops

In the gRPC core implementation, a fundamental struct is the
`grpc_transport_stream_op_batch` which represents a collection of stream
operations sent to a transport. (Note that in gRPC, _stream_ and _RPC_ are used
synonymously since all RPCs are actually streams internally.) The ops in a batch
can include:

* send\_initial\_metadata
  - Client: initiate an RPC
  - Server: supply response headers
* recv\_initial\_metadata
  - Client: get response headers
  - Server: accept an RPC
* send\_message (zero or more) : send a data buffer
* recv\_message (zero or more) : receive a data buffer
* send\_trailing\_metadata
  - Client: half-close indicating that no more messages will be coming
  - Server: full-close providing final status for the RPC
* recv\_trailing\_metadata: get final status for the RPC
  - Server extra: This op shouldn't actually be considered complete until the
    server has also sent trailing metadata to provide the other side with final
    status
* cancel\_stream: Attempt to cancel an RPC
* collect\_stats: Get stats

The fundamental responsibility of the transport is to transform between this
internal format and an actual wire format, so the processing of these operations
is largely transport-specific.

One or more of these ops are grouped into a batch. Applications can start all of
a call's ops in a single batch, or they can split them up into multiple
batches. Results of each batch are returned asynchronously via a completion
queue.

Internally, we use callbacks to indicate completion. The surface layer creates a
callback when starting a new batch and sends it down the filter stack along with
the batch. The transport must invoke this callback when the batch is complete,
and then the surface layer returns an event to the application via the
completion queue. Each batch can have up to 3 callbacks:

* recv\_initial\_metadata\_ready (called by the transport when the
  recv\_initial\_metadata op is complete)
* recv\_message\_ready (called by the transport when the recv_message op is
  complete)
* on\_complete (called by the transport when the entire batch is complete)

## Timelines of transport stream op batches

The transport's job is to sequence and interpret various possible interleavings
of the basic stream ops. For example, a sample timeline of batches would be:

1. Client send\_initial\_metadata: Initiate an RPC with a path (method) and authority
1. Server recv\_initial\_metadata: accept an RPC
1. Client send\_message: Supply the input proto for the RPC
1. Server recv\_message: Get the input proto from the RPC
1. Client send\_trailing\_metadata: This is a half-close indicating that the
   client will not be sending any more messages
1. Server recv\_trailing\_metadata: The server sees this from the client and
   knows that it will not get any more messages. This won't complete yet though,
   as described above.
1. Server send\_initial\_metadata, send\_message, send\_trailing\_metadata: A
   batch can contain multiple ops, and this batch provides the RPC response
   headers, response content, and status. Note that sending the trailing
   metadata will also complete the server's receive of trailing metadata.
1. Client recv\_initial\_metadata: The number of ops in one side of the batch
   has no relation with the number of ops on the other side of the batch. In
   this case, the client is just collecting the response headers.
1. Client recv\_message, recv\_trailing\_metadata: Get the data response and
   status


There are other possible sample timelines. For example, for client-side streaming, a "typical" sequence would be:

1. Server: recv\_initial\_metadata
   - At API-level, that would be the server requesting an RPC
1. Server: recv\_trailing\_metadata
   - This is for when the server wants to know the final completion of the RPC
     through an `AsyncNotifyWhenDone` API in C++
1. Client: send\_initial\_metadata, recv\_message, recv\_trailing\_metadata
   - At API-level, that's a client invoking a client-side streaming call. The
     send\_initial\_metadata is the call invocation, the recv\_message collects
     the final response from the server, and the recv\_trailing\_metadata gets
     the `grpc::Status` value that will be returned from the call
1. Client: send\_message / Server: recv\_message
   - Repeat the above step numerous times; these correspond to a client issuing
     `Write` in a loop and a server doing `Read` in a loop until `Read` fails
1. Client: send\_trailing\_metadata / Server: recv\_message that indicates doneness (NULL)
   - These correspond to a client issuing `WritesDone` which causes the server's
     `Read` to fail
1. Server: send\_message, send\_trailing\_metadata
   - These correspond to the server doing `Finish`

The sends on one side will call their own callbacks when complete, and they will
in turn trigger actions that cause the other side's recv operations to
complete. In some transports, a send can sometimes complete before the recv on
the other side (e.g., in HTTP/2 if there is sufficient flow-control buffer space
available)

## Other transport duties

In addition to these basic stream ops, the transport must handle cancellations
of a stream at any time and pass their effects to the other side. For example,
in HTTP/2, this triggers a `RST_STREAM` being sent on the wire. The transport
must perform operations like pings and statistics that are used to shape
transport-level characteristics like flow control (see, for example, their use
in the HTTP/2 transport).

## Putting things together with detail: Sending Metadata

* API layer: `map<string, string>` that is specific to this RPC
* Core surface layer: array of `{slice, slice}` pairs where each slice
  references an underlying string
* [Core transport
  layer](https://github.com/grpc/grpc/tree/master/src/core/lib/transport): list
  of `{slice, slice}` pairs that includes the above plus possibly some general
  metadata (e.g., Method and Authority for initial metadata)
* [Specific transport
  layer](https://github.com/grpc/grpc/tree/master/src/core/ext/transport):
  - Either send it to the other side using transport-specific API (e.g., Cronet)
  - Or have it sent through the [iomgr/endpoint
    layer](https://github.com/grpc/grpc/tree/master/src/core/lib/iomgr) (e.g.,
    HTTP/2)
  - Or just manipulate pointers to get it from one side to the other (e.g.,
    In-process)

## Requirements for any transport

Each transport implements several operations in a vtbl (may change to actual
virtual functions as transport moves to idiomatic C++).

The most important and common one is `perform_stream_op`. This function
processes a single stream op batch on a specific stream that is associated with
a specific transport:

* Gets the 6 ops/cancel passed down from the surface
* Pass metadata from one side to the other as described above
* Transform messages between slice buffer structure and stream of bytes to pass
  to other side
  - May require insertion of extra bytes (e.g., per-message headers in HTTP/2)
* React to metadata to preserve expected orderings (*)
* Schedule invocation of completion callbacks

There are other functions in the vtbl as well.

* `perform_transport_op`
  - Configure the transport instance for the connectivity state change notifier
    or the server-side accept callback
  - Disconnect transport or set up a goaway for later streams
* `init_stream`
  - Starts a stream from the client-side
  - (*) Server-side of the transport must call `accept_stream_cb` when a new
  stream is available
    * Triggers request-matcher
* `destroy_stream`, `destroy_transport`
  - Free up data related to a stream or transport
* `set_pollset`, `set_pollset_set`, `get_endpoint`
  - Map each specific instance of the transport to FDs being used by iomgr (for
    HTTP/2)
  - Get a pointer to the endpoint structure that actually moves the data
    (wrapper around a socket for HTTP/2)

## Book-keeping responsibilities of the transport layer

A given transport must keep all of its transport and streams ref-counted. This
is essential to make sure that no struct disappears before it is done being
used.

A transport must also preserve relevant orders for the different categories of
ops on a stream, as described above. A transport must also make sure that all
relevant batch operations have completed before scheduling the `on_complete`
closure for a batch. Further examples include the idea that the server logic
expects to not complete recv\_trailing\_metadata until after it actually sends
trailing metadata since it would have already found this out by seeing a NULL’ed
recv\_message. This is considered part of the transport's duties in preserving
orders.
