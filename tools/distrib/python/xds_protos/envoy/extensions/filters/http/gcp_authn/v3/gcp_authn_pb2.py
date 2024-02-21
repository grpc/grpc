# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


from envoy.config.core.v3 import base_pb2 as envoy_dot_config_dot_core_dot_v3_dot_base__pb2
from envoy.config.core.v3 import http_uri_pb2 as envoy_dot_config_dot_core_dot_v3_dot_http__uri__pb2
from google.protobuf import wrappers_pb2 as google_dot_protobuf_dot_wrappers__pb2
from udpa.annotations import status_pb2 as udpa_dot_annotations_dot_status__pb2
from validate import validate_pb2 as validate_dot_validate__pb2


DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n:envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.proto\x12*envoy.extensions.filters.http.gcp_authn.v3\x1a\x1f\x65nvoy/config/core/v3/base.proto\x1a#envoy/config/core/v3/http_uri.proto\x1a\x1egoogle/protobuf/wrappers.proto\x1a\x1dudpa/annotations/status.proto\x1a\x17validate/validate.proto\"\xad\x02\n\x14GcpAuthnFilterConfig\x12\x39\n\x08http_uri\x18\x01 \x01(\x0b\x32\x1d.envoy.config.core.v3.HttpUriB\x08\xfa\x42\x05\x8a\x01\x02\x10\x01\x12\x37\n\x0cretry_policy\x18\x02 \x01(\x0b\x32!.envoy.config.core.v3.RetryPolicy\x12R\n\x0c\x63\x61\x63he_config\x18\x03 \x01(\x0b\x32<.envoy.extensions.filters.http.gcp_authn.v3.TokenCacheConfig\x12M\n\x0ctoken_header\x18\x04 \x01(\x0b\x32\x37.envoy.extensions.filters.http.gcp_authn.v3.TokenHeader\" \n\x08\x41udience\x12\x14\n\x03url\x18\x01 \x01(\tB\x07\xfa\x42\x04r\x02\x10\x01\"U\n\x10TokenCacheConfig\x12\x41\n\ncache_size\x18\x01 \x01(\x0b\x32\x1c.google.protobuf.UInt64ValueB\x0f\xfa\x42\x0c\x32\n\x18\xff\xff\xff\xff\xff\xff\xff\xff\x7f\"M\n\x0bTokenHeader\x12\x1b\n\x04name\x18\x01 \x01(\tB\r\xfa\x42\nr\x08\x10\x01\xc0\x01\x01\xc8\x01\x00\x12!\n\x0cvalue_prefix\x18\x02 \x01(\tB\x0b\xfa\x42\x08r\x06\xc0\x01\x02\xc8\x01\x00\x42\xb2\x01\n8io.envoyproxy.envoy.extensions.filters.http.gcp_authn.v3B\rGcpAuthnProtoP\x01Z]github.com/envoyproxy/go-control-plane/envoy/extensions/filters/http/gcp_authn/v3;gcp_authnv3\xba\x80\xc8\xd1\x06\x02\x10\x02\x62\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'envoy.extensions.filters.http.gcp_authn.v3.gcp_authn_pb2', _globals)
if _descriptor._USE_C_DESCRIPTORS == False:
  DESCRIPTOR._options = None
  DESCRIPTOR._serialized_options = b'\n8io.envoyproxy.envoy.extensions.filters.http.gcp_authn.v3B\rGcpAuthnProtoP\001Z]github.com/envoyproxy/go-control-plane/envoy/extensions/filters/http/gcp_authn/v3;gcp_authnv3\272\200\310\321\006\002\020\002'
  _GCPAUTHNFILTERCONFIG.fields_by_name['http_uri']._options = None
  _GCPAUTHNFILTERCONFIG.fields_by_name['http_uri']._serialized_options = b'\372B\005\212\001\002\020\001'
  _AUDIENCE.fields_by_name['url']._options = None
  _AUDIENCE.fields_by_name['url']._serialized_options = b'\372B\004r\002\020\001'
  _TOKENCACHECONFIG.fields_by_name['cache_size']._options = None
  _TOKENCACHECONFIG.fields_by_name['cache_size']._serialized_options = b'\372B\0142\n\030\377\377\377\377\377\377\377\377\177'
  _TOKENHEADER.fields_by_name['name']._options = None
  _TOKENHEADER.fields_by_name['name']._serialized_options = b'\372B\nr\010\020\001\300\001\001\310\001\000'
  _TOKENHEADER.fields_by_name['value_prefix']._options = None
  _TOKENHEADER.fields_by_name['value_prefix']._serialized_options = b'\372B\010r\006\300\001\002\310\001\000'
  _globals['_GCPAUTHNFILTERCONFIG']._serialized_start=265
  _globals['_GCPAUTHNFILTERCONFIG']._serialized_end=566
  _globals['_AUDIENCE']._serialized_start=568
  _globals['_AUDIENCE']._serialized_end=600
  _globals['_TOKENCACHECONFIG']._serialized_start=602
  _globals['_TOKENCACHECONFIG']._serialized_end=687
  _globals['_TOKENHEADER']._serialized_start=689
  _globals['_TOKENHEADER']._serialized_end=766
# @@protoc_insertion_point(module_scope)
