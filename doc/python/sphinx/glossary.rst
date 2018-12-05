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

  status code
    The gRPC status code defined in https://github.com/grpc/grpc/blob/master/doc/statuscodes.md.

  status message
    A text (unicode allowed) field that indicates the status of the RPC call and it is intended to be read by the developers.

  status details
    A encoded *google.rpc.status.Status* proto message that serves as rich status error details about the RPC Call.
