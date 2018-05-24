# gRPC semantics

gRPC has many features, but the most fundamental core is the ability to perform
RPCs. The goal of this document is defining the semantics of gRPC's RPCs.

## Channels

Most implementations will have Channel and Server concepts. A Channel is a
virtual connection to an endpoint, capable of sending RPCs. Channel is "virtual"
because the Channel is free to have zero or many actual connections. A Channel
is free to determine which actual endpoint to use and may change it every RPC,
permitting client-side load balancing. A Server is capable of receiving incoming
connections and receiving RPCs.

A "connection" is not a gRPC semantic concept and thus users should not assume a
correlation between connections and RPCs. Although, practically, users should be
aware of the common restriction that RPCs are unable to survive longer than the
connection on which they exist.

The exact details of how the communication is performed and higher level
abstractions can change when necessary and are thus not a focus for this
document. Although implementations SHOULD support the [HTTP/2
transport](PROTOCOL-HTTP2.md) to provide a basis for interoperability.

## Methods

An RPC is performed on a Method. The Method has a name, defines the intended
operation, the message types involved with the RPC, and the cardinality of
those messages.  It does not define the endpoint to send the RPC to; this is
normally handled by the Channel.

A Method's intended operation is just normal documentation describing what a
Method does, intended for a developer. gRPC itself is not generally aware of the
intended operation.

A Method has a request message type and a separate response message type. gRPC
is only aware of these types well enough to serialize and deserialize them.
Messages are considered opaque byte sequences of a known length to gRPC itself.

A Method's request and response each have a cardinality: either one ("unary"),
or zero to many (a "stream"). This produces four possible configurations which,
for convenience, each have a name:

|                       | **unary response** | **streaming response** |
| --------------------- | ------------------ | ---------------------- |
| **unary request**     | unary              | server-streaming       |
| **streaming request** | client-streaming   | bidirectional (bidi)   |

While we use the term "method," pedantically it is closer to "function" as it is
not object oriented and there is no "receiver" involved (the `this` variable in
many languages) other than the destination machine. gRPC is based on message
passing, not object orientation.

Related Methods are typically grouped into a Service. To gRPC, a Service is a
group of methods that tend to be implemented together and that all share the
Service's namespace. A Service is a higher-level abstraction and may not be
present explicitly in all implementations. However, the namespace provided by a
Service is a core distinguishing feature of its Methods; if two Methods have the
same name but exist in different Services they must be considered distinct and
not be confused. A Method name including its Service namespace prefix with a "/"
separator is a "full method name".

## Calls

RPCs, or "Calls," are initiated by a client to a server, typically via a
Channel. There may be multiple servers that _could_ have received the Call (as
is common for load balancing), but only a single server may process an
individual Call. Calls are assumed to be non-idempotent and may not be
"replayed" except for when gRPC is explicitly informed it is safe to do so.

Calls are natively two independent streams (i.e., full duplex bidirectional) of
Messages. The request stream is started with Request Headers and ended by Half
Close. The response stream is started with Response Headers and ended by
Trailers, or consistes only of Trailers. Messages may exist between the headers
and the end of the stream. Request Headers, Response Headers, Messages, Half
Close, and Trailers are the units of communication and, absent the Call's
termination, will be communicated to the remote without the need to send further
units on the stream. However, see the optimizations permitted for unary Calls
below.

Request Headers contain the Full Method Name and Metadata. Response Headers
contain Metadata. Trailers contain the Status and Metadata. Messages contain the
Message Payload. These contents are not exhaustive; gRPC features may extend
these concepts. It is quite common for features to add additional fields to
Request Headers, Response Headers, and Trailers.

The Call initiation is with Request Headers, within which the client
indicates the method to be run by its Full Method Name. The Call is gracefully
completed when the server responds with Trailers, which contains a Status
communicating the success or failure of the RPC. If a server responds with
Trailers before receiving the client's Half Close, then any unprocessed
client-sent Messages and Half Close is lost. Note that on the server there is a
period of time between when the server application responds with a Trailers and
when that Trailers is actually sent; the Call is only truly complete when the
Trailers is sent. Similarly, on the client there is a period between the gRPC
implementation receiving the Trailers and when the application receives the
Trailers; the Call is only truly complete when the Trailers is received by the
application.

Calls may terminate early by being "cancelled." Implementations must allow
clients to cancel Calls, but cancellations may occur in other ways like I/O
failures. A cancellation appears as a Trailers with a Status Code of CANCELLED
to clients and is a special state on servers. Cancellation is an abrupt killing
of the Call; inbound and outbound buffered data should be cleared. Cancellation
trumps graceful completion; if the client gRPC implementation received the
Trailers before the cancellation, yet the client application has not received
the Trailers, then cancellation generally should win. No auxilary information is
included in cancellation signals between the client and server. Server
implementations may fail a Call and respond with Trailers while claiming to the
server application that the Call was cancelled.

The two independent streams are each unidirectional and do not provide any
information in the reverse direction other than Flow Control. Flow Control
is a signal from the receiver to the sender to temporarily pause sending
additional messages to avoid excessive buffering. Flow Control only applies to
messages, but since streams are in-order Half Close or Trailers may be delayed
waiting for message Flow Control in the same stream. No message receipt
acknowledgement information is provided. However applications may use messages
for such signals, as a response naturally acknowledges its request. Note in
particular that there is no provision for the server application to not know
whether the client application received a unary Call's response or a streaming
Call's Status.

Unary Calls may be optimized to be half-duplex and treat each stream as a single
communication unit. That is, on the client a unary Call may be delayed from
being sent until the Half Close is ready to be sent and on the server the
response may be delayed until the Trailers is ready.

Unary Calls that terminate with a Status Code of OK must contain a response
message. Unary Calls that terminate with a Status Code other than OK do not need
a response message, and at the implementation's discretion the response message
may be discarded if present.

## Status

A Status contains a "code" and a "description". The Status Description is a
human-readable Unicode string for developer debugging. The Status Code is a
value from a pre-defined list of such codes. While Status Code is best
communicated to users by its name, it commonly is treated as an integer
internally, and so each code has a numeric value.

The valid Status Codes are:

| Num | Name                |
| --- | ------------------- |
| 0   | OK                  |
| 1   | CANCELLED           |
| 2   | UNKNOWN             |
| 3   | INVALID_ARGUMENT    |
| 4   | DEADLINE_EXCEEDED   |
| 5   | NOT_FOUND           |
| 6   | ALREADY_EXISTS      |
| 7   | PERMISSION_DENIED   |
| 8   | RESOURCE_EXHAUSTED  |
| 9   | FAILED_PRECONDITION |
| 10  | ABORTED             |
| 11  | OUT_OF_RANGE        |
| 12  | UNIMPLEMENTED       |
| 13  | INTERNAL            |
| 14  | UNAVAILABLE         |
| 15  | DATA_LOSS           |
| 16  | UNAUTHENTICATED     |

Using the OK Status Code for a Call may only be decided by server applications.
Library implementations must not "fabricate" an OK Status Code; it may only
communicate an OK Status Code that was provided to it. While there may be
additional restrictions on Status Code usage like those detailed in
[statuscodes.md](statuscodes.md), those restrictions are more to provide a
cohesive experience instead of a core, fundamental requirement.

## Metadata

Metadata has keys with associated values. Each key can have multiple values.
Keys are unordered, but values for a key are ordered. Keys are case insensitive,
and commonly canonicalized by making lower case. APIs are permitted to require a
canonical representation and that representation may be different than the
"lower case" representation mentioned here. A key can be for ASCII or binary
values. If a key is for binary values, its name must be suffixed with "-bin".
Otherwise it is for ASCII values.

ASCII values are discouraged from having leading or trailing whitespace. If such
a value contains leading or trailing whitespace, the whitespace may be stripped.
Multiple ASCII values for the same key may be joined together with "," (a comma)
as the delimiter and be considered semantically equivalent to the multi-value
form.  However, such a transformation is lossy; an arbitrary ASCII value may not
be split on comma and be assumed to be equivalent to a valid multi-value form
for its key.

The position of the Metadata in either Headers or Trailers is semantically
important. Metadata from a Headers may not be moved to Trailers or vise-versa
without additional knowledge of the individual key semantics.

## Additional Features

Although these are "additional" features, that does not make them unimportant.
The are additional because they do not need to be represented as a fundamental
gRPC protocol concept.

### Deadline Propagation

It is a common concept for RPCs to have a Deadline, a point in time by which the
Call needs to complete before the client will give up. If the Client has a
Deadline for the Call it should be included in the Request Headers. Calls are
not required to have a Deadline. If a Call has a Deadline, both the client and
server should track the Deadline and Cancel the Call when the Deadline is
reached. When the client Cancels the Call, it should report a Trailers
containing a Status Code of DEADLINE_EXCEEDED.

While the feature is called "Deadline" and involves Deadlines from the user's
perspective, it is generally communicated with a timeout, which is a duration
instead of a point in time. Communicating a duration avoids the need for civil
time clock synchronization between the client and server.

By its very nature, the client and server's Deadline will be slightly different
and their detection of its expiration will race. When clients receive a
Cancellation on a Call with a Deadline, they should double-check whether the
Deadline has passed. If it has passed, they should override the cancellation
with Trailers with one that has a Status Code of DEADLINE_EXCEEDED.

### Authority

Virtual hosting is a common concept where a server supports multiple identities
and supports different services depending on the identity. "Identity" is a
server name, in "host" or "host:port" form as found for the "authority" portion
of URIs. Transports able to support virtual hosting should transmit the
authority the client identifies the service as and make that available to the
server application. The authority would appear to be included in the Request
Headers to the server application.

# Appendix

## Appendix A: Call ABNF

A conceptual Call, ignoring cancellation signaling, in
[ABNF syntax](http://tools.ietf.org/html/rfc5234):
```
call              = request-stream response-stream ; in parallel
request-stream    = request-headers *request [half-close]
response-stream   = [response-headers *response] trailers

request-headers   = full-method-name metadata
request           = message
half-close        = ; No content
response-headers  = metadata
response          = message
trailers          = status metadata

full-method-name  = full-service-name "/" short-method-name
full-service-name = 1*VCHAR ; TODO. Is not precisely defined
short-method-name = 1*VCHAR ; TODO. Is not precisely defined
                            ; Probably should not include "/"

metadata          = *(ascii-metadata / binary-metadata)
metadata-key-char = 1*(DIGIT / ALPHA / "_" / "-" / ".")
ascii-metadata    = ascii-key ascii-value
binary-metadata   = binary-key *binary-value
ascii-key         = 1*metadata-key-char ; must not end in "-bin"
ascii-value       = 1*(SP / VCHAR)
binary-key        = 1*metadata-key-char "-bin"
binary-value      = 1*OCTET ; TODO: do we support zero-length values?

message           = message-payload
message-payload   = *OCTET ; zero-byte payloads are supported

status            = status-code [status-desc]
status-code       = <see enumeration in Status section>
status-desc       = 1*<unicode-char>
```
