gRPC Internationalization
=========================

As a universal RPC framework, gRPC needs to be fully usable within/across different international environments. 
This document describes gRPC API and behavior specifics when used in a non-english environment.

## API Concepts

While some API elements need to be able to represent non-english content, some are intentionally left as ASCII-only
for simplicity & performance reasons.

### Method name (in RPC Invocation)
Method names are ASCII-only. Most gRPC services will use protobuf which only allows ASCII based method names anyway.
Also, handling method names is a very hot code path.

Recommended representation in language-specific APIs: string type.

### Host name (in RPC Invocation)
Host names are punycode encoded. Currently, the punycode needs to be provided by the user.

Recommended representation in language-specific APIs: string/unicode string.

NOTE: overriding host name when invoking RPCs is only supported by C-core based gRPC implementations.

### Status detail/message (accompanies RPC status code)

Status messages are expected to contain national-alphabet characters.
Allowed values are unicode strings (content will be percent-encoded on the wire).

Recommended representation in language-specific APIs: unicode string.

### Metadata key
Allowed values are defined by HTTP/2 standard (metadata keys are represented as HTTP/2 header/trailer names).

Recommended representation in language-specific APIs: string.

### Metadata value (text-valued metadata)
Allowed values are defined by HTTP/2 standard (metadata values are represented as HTTP/2 header/trailer text values).

Recommended representation in language-specific APIs: string.

### Channel name

TBD
