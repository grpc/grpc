# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: envoy/extensions/transport_sockets/quic/v3/quic_transport.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


from envoy.extensions.transport_sockets.tls.v3 import tls_pb2 as envoy_dot_extensions_dot_transport__sockets_dot_tls_dot_v3_dot_tls__pb2
from google.protobuf import wrappers_pb2 as google_dot_protobuf_dot_wrappers__pb2
from udpa.annotations import status_pb2 as udpa_dot_annotations_dot_status__pb2
from validate import validate_pb2 as validate_dot_validate__pb2


DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n?envoy/extensions/transport_sockets/quic/v3/quic_transport.proto\x12*envoy.extensions.transport_sockets.quic.v3\x1a\x33\x65nvoy/extensions/transport_sockets/tls/v3/tls.proto\x1a\x1egoogle/protobuf/wrappers.proto\x1a\x1dudpa/annotations/status.proto\x1a\x17validate/validate.proto\"\xbb\x01\n\x17QuicDownstreamTransport\x12i\n\x16\x64ownstream_tls_context\x18\x01 \x01(\x0b\x32?.envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContextB\x08\xfa\x42\x05\x8a\x01\x02\x10\x01\x12\x35\n\x11\x65nable_early_data\x18\x02 \x01(\x0b\x32\x1a.google.protobuf.BoolValue\"~\n\x15QuicUpstreamTransport\x12\x65\n\x14upstream_tls_context\x18\x01 \x01(\x0b\x32=.envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContextB\x08\xfa\x42\x05\x8a\x01\x02\x10\x01\x42\xb2\x01\n8io.envoyproxy.envoy.extensions.transport_sockets.quic.v3B\x12QuicTransportProtoP\x01ZXgithub.com/envoyproxy/go-control-plane/envoy/extensions/transport_sockets/quic/v3;quicv3\xba\x80\xc8\xd1\x06\x02\x10\x02\x62\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'envoy.extensions.transport_sockets.quic.v3.quic_transport_pb2', _globals)
if _descriptor._USE_C_DESCRIPTORS == False:
  DESCRIPTOR._options = None
  DESCRIPTOR._serialized_options = b'\n8io.envoyproxy.envoy.extensions.transport_sockets.quic.v3B\022QuicTransportProtoP\001ZXgithub.com/envoyproxy/go-control-plane/envoy/extensions/transport_sockets/quic/v3;quicv3\272\200\310\321\006\002\020\002'
  _QUICDOWNSTREAMTRANSPORT.fields_by_name['downstream_tls_context']._options = None
  _QUICDOWNSTREAMTRANSPORT.fields_by_name['downstream_tls_context']._serialized_options = b'\372B\005\212\001\002\020\001'
  _QUICUPSTREAMTRANSPORT.fields_by_name['upstream_tls_context']._options = None
  _QUICUPSTREAMTRANSPORT.fields_by_name['upstream_tls_context']._serialized_options = b'\372B\005\212\001\002\020\001'
  _globals['_QUICDOWNSTREAMTRANSPORT']._serialized_start=253
  _globals['_QUICDOWNSTREAMTRANSPORT']._serialized_end=440
  _globals['_QUICUPSTREAMTRANSPORT']._serialized_start=442
  _globals['_QUICUPSTREAMTRANSPORT']._serialized_end=568
# @@protoc_insertion_point(module_scope)
