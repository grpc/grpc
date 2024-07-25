# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: envoy/extensions/http/injected_credentials/generic/v3/generic.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


from envoy.extensions.transport_sockets.tls.v3 import secret_pb2 as envoy_dot_extensions_dot_transport__sockets_dot_tls_dot_v3_dot_secret__pb2
from xds.annotations.v3 import status_pb2 as xds_dot_annotations_dot_v3_dot_status__pb2
from udpa.annotations import status_pb2 as udpa_dot_annotations_dot_status__pb2
from validate import validate_pb2 as validate_dot_validate__pb2


DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\nCenvoy/extensions/http/injected_credentials/generic/v3/generic.proto\x12\x35\x65nvoy.extensions.http.injected_credentials.generic.v3\x1a\x36\x65nvoy/extensions/transport_sockets/tls/v3/secret.proto\x1a\x1fxds/annotations/v3/status.proto\x1a\x1dudpa/annotations/status.proto\x1a\x17validate/validate.proto\"\x80\x01\n\x07Generic\x12X\n\ncredential\x18\x01 \x01(\x0b\x32:.envoy.extensions.transport_sockets.tls.v3.SdsSecretConfigB\x08\xfa\x42\x05\x8a\x01\x02\x10\x01\x12\x1b\n\x06header\x18\x02 \x01(\tB\x0b\xfa\x42\x08r\x06\xc0\x01\x01\xd0\x01\x01\x42\xcd\x01\nCio.envoyproxy.envoy.extensions.http.injected_credentials.generic.v3B\x0cGenericProtoP\x01Zfgithub.com/envoyproxy/go-control-plane/envoy/extensions/http/injected_credentials/generic/v3;genericv3\xba\x80\xc8\xd1\x06\x02\x10\x02\xd2\xc6\xa4\xe1\x06\x02\x08\x01\x62\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'envoy.extensions.http.injected_credentials.generic.v3.generic_pb2', _globals)
if _descriptor._USE_C_DESCRIPTORS == False:
  DESCRIPTOR._options = None
  DESCRIPTOR._serialized_options = b'\nCio.envoyproxy.envoy.extensions.http.injected_credentials.generic.v3B\014GenericProtoP\001Zfgithub.com/envoyproxy/go-control-plane/envoy/extensions/http/injected_credentials/generic/v3;genericv3\272\200\310\321\006\002\020\002\322\306\244\341\006\002\010\001'
  _GENERIC.fields_by_name['credential']._options = None
  _GENERIC.fields_by_name['credential']._serialized_options = b'\372B\005\212\001\002\020\001'
  _GENERIC.fields_by_name['header']._options = None
  _GENERIC.fields_by_name['header']._serialized_options = b'\372B\010r\006\300\001\001\320\001\001'
  _globals['_GENERIC']._serialized_start=272
  _globals['_GENERIC']._serialized_end=400
# @@protoc_insertion_point(module_scope)
