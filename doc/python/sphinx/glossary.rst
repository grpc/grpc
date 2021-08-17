Glossary
================

.. glossary::

  metadatum
    A key-value pair included in the HTTP header.  It is a
    2-tuple where the first entry is the key and the
    second is the value, i.e. (key, value).  The metadata key is an ASCII str,
    and must be a valid HTTP header name.  The metadata value can be
    either a valid HTTP ASCII str, or bytes.  If bytes are provided,
    the key must end with '-bin', i.e.
    ``('binary-metadata-bin', b'\\x00\\xFF')``

  metadata
    A sequence of metadatum.

  serializer
    A callable function that encodes an object into bytes. Applications are
    allowed to provide any customized serializer, so there isn't a restriction
    for the input object (i.e. even ``None``). On the server-side, the
    serializer is invoked with server handler's return value; on the
    client-side, the serializer is invoked with outbound message objects.

  deserializer
    A callable function that decodes bytes into an object. Same as serializer,
    the returned object doesn't have restrictions (i.e. ``None`` allowed). The
    deserializer is invoked with inbound message bytes on both the server side
    and the client-side.

  wait_for_ready
    If an RPC is issued but the channel is in the TRANSIENT_FAILURE or SHUTDOWN
    states, the library cannot transmit the RPC at the moment. By default, the
    gRPC library will fail such RPCs immediately. This is known as "fail fast."
    RPCs will not fail as a result of the channel being in other states
    (CONNECTING, READY, or IDLE).

    When the wait_for_ready option is specified, the library will queue RPCs
    until the channel is READY. Any submitted RPCs may still fail before the
    READY state is reached for other reasons, e.g., the client channel has been
    shut down or the RPC's deadline has been reached.

  channel_arguments
    A list of key-value pairs to configure the underlying gRPC Core channel or
    server object. Channel arguments are meant for advanced usages and contain
    experimental API (some may not labeled as experimental). Full list of
    available channel arguments and documentation can be found under the
    "grpc_arg_keys" section of "grpc_types.h" header file (|grpc_types_link|).
    For example, if you want to disable TCP port reuse, you may construct
    channel arguments like: ``options = (('grpc.so_reuseport', 0),)``.
