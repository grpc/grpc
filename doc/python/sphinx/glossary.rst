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
