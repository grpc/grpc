## **gRPC Compression**

The keywords "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD",
"SHOULD NOT", "RECOMMENDED",  "MAY", and "OPTIONAL" in this document are to be
interpreted as described in [RFC 2119](http://www.ietf.org/rfc/rfc2119.txt).

### Intent

Compression is used to reduce the amount of bandwidth used between peers. The
compression supported by gRPC acts _at the individual message level_, taking
_message_ [as defined in the wire format
document](PROTOCOL-HTTP2.md).

The implementation supports different compression algorithms. A _default
compression level_, to be used in the absence of message-specific settings, MAY
be specified for during channel creation.

The ability to control compression settings per call and to enable/disable
compression on a per message basis MAY be used to prevent CRIME/BEAST attacks.
It also allows for asymmetric compression communication, whereby a response MAY
be compressed differently, if at all.

### Specification

Compression MAY be configured by the Client Application by calling the
appropriate API method. There are two scenarios where compression MAY be
configured:

+  At channel creation time, which sets the channel default compression and
   therefore the compression that SHALL be used in the absence of per-RPC
   compression configuration.
+  At response time, via:
   +  For unary RPCs, the {Client,Server}Context instance. 
   +  For streaming RPCs, the {Client,Server}Writer instance. In this case,
      configuration is reduced to disabling compression altogether.

### Compression Method Asymmetry Between Peers

A gRPC peer MAY choose to respond using a different compression method to that
of the request, including not performing any compression, regardless of channel
and RPC settings (for example, if compression would result in small or negative
gains).

A compressed message from a client with an algorithm unsupported by a server,
WILL result in an INVALID\_ARGUMENT error, alongside the receiving peer's
`grpc-accept-encoding` header specifying the algorithms it accepts. If an
INTERNAL error is returned from the server despite having used one of the
algorithms from the `grpc-accept-encoding h`eader, the cause MUST NOT be related
to compression. Data sent from a server compressed with an algorithm not
supported by the client will also result in an INTERNAL error.

Note that a peer MAY choose to not disclose all the encodings it supports.
However, if it receives a message compressed in an undisclosed but supported
encoding, it MUST include said encoding in the response's `grpc-accept-encoding
h`eader.

For every message a server is requested to compress using an algorithm it knows
the client doesn't support (as indicated by the last `grpc-accept-encoding`
header received from the client), it SHALL send the message uncompressed. 

### Specific Disabling of Compression

If the user (through the previously described mechanisms) requests to disable
compression the next message MUST be sent uncompressed. This is instrumental in
preventing BEAST/CRIME attacks. This applies to both the the unary and streaming
cases.

### Compression Levels and Algorithms

We currently (as of July 2015) support _gzip_ and _deflate_ as algorithms (with
the possible future addition of snappy). In order to simplify the public API,
it's intended to abstract the algorithms as _compression levels_ (such as "low",
"medium", "high") that'd map to concrete algorithms and/or their settings (such
as "low" mapping to "gzip -3" and "high" mapping to "gzip -9"). However, we
can't presently (July 2015) implement said compression levels at the client side
without either a initial negotiation of capabilities or an automatic retry
mechanism. Therefore, compression levels are only supported at the server side,
which is aware of the client's capabilities by virtue of the incoming
Message-Accept-Encoding header.

### Propagation to child RPCs

The inheritance of the compression configuration by child RPCs is left up to the
implementation. Note that in the absence of changes to the parent channel, its
configuration will be used.

### Test cases

1. When a compression level is not specified for either the channel or the
message, the default channel level _none_ is considered: data MUST NOT be
compressed.
1. When per-RPC compression configuration isn't present for a message, the
channel compression configuration MUST be used.
1. When a compression method (including no compression) is specified for an
outgoing message, the message MUST be compressed accordingly.
1. A message compressed in a way not supported by its endpoint MUST fail with
INVALID\_ARGUMENT status, its associated description indicating the unsupported
condition as well as the supported ones. The returned `grpc-accept-encoding`
header MUST NOT contain the compression method (encoding) used.
1. An ill-constructed message with its [Compressed-Flag
bit](PROTOCOL-HTTP2.md#compressed-flag)
set but lacking a
"[grpc-encoding](PROTOCOL-HTTP2.md#message-encoding)"
entry different from _identity_ in its metadata MUST fail with INTERNAL status,
its associated description indicating the invalid Compressed-Flag condition.
