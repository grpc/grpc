# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: envoy/api/v2/scoped_route.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


from udpa.annotations import migrate_pb2 as udpa_dot_annotations_dot_migrate__pb2
from udpa.annotations import status_pb2 as udpa_dot_annotations_dot_status__pb2
from validate import validate_pb2 as validate_dot_validate__pb2


DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n\x1f\x65nvoy/api/v2/scoped_route.proto\x12\x0c\x65nvoy.api.v2\x1a\x1eudpa/annotations/migrate.proto\x1a\x1dudpa/annotations/status.proto\x1a\x17validate/validate.proto\"\xa8\x02\n\x18ScopedRouteConfiguration\x12\x15\n\x04name\x18\x01 \x01(\tB\x07\xfa\x42\x04r\x02 \x01\x12)\n\x18route_configuration_name\x18\x02 \x01(\tB\x07\xfa\x42\x04r\x02 \x01\x12\x41\n\x03key\x18\x03 \x01(\x0b\x32*.envoy.api.v2.ScopedRouteConfiguration.KeyB\x08\xfa\x42\x05\x8a\x01\x02\x10\x01\x1a\x86\x01\n\x03Key\x12P\n\tfragments\x18\x01 \x03(\x0b\x32\x33.envoy.api.v2.ScopedRouteConfiguration.Key.FragmentB\x08\xfa\x42\x05\x92\x01\x02\x08\x01\x1a-\n\x08\x46ragment\x12\x14\n\nstring_key\x18\x01 \x01(\tH\x00\x42\x0b\n\x04type\x12\x03\xf8\x42\x01\x42\x90\x01\n\x1aio.envoyproxy.envoy.api.v2B\x10ScopedRouteProtoP\x01Z9github.com/envoyproxy/go-control-plane/envoy/api/v2;apiv2\xf2\x98\xfe\x8f\x05\x17\x12\x15\x65nvoy.config.route.v3\xba\x80\xc8\xd1\x06\x02\x10\x01\x62\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'envoy.api.v2.scoped_route_pb2', _globals)
if _descriptor._USE_C_DESCRIPTORS == False:
  DESCRIPTOR._options = None
  DESCRIPTOR._serialized_options = b'\n\032io.envoyproxy.envoy.api.v2B\020ScopedRouteProtoP\001Z9github.com/envoyproxy/go-control-plane/envoy/api/v2;apiv2\362\230\376\217\005\027\022\025envoy.config.route.v3\272\200\310\321\006\002\020\001'
  _SCOPEDROUTECONFIGURATION_KEY_FRAGMENT.oneofs_by_name['type']._options = None
  _SCOPEDROUTECONFIGURATION_KEY_FRAGMENT.oneofs_by_name['type']._serialized_options = b'\370B\001'
  _SCOPEDROUTECONFIGURATION_KEY.fields_by_name['fragments']._options = None
  _SCOPEDROUTECONFIGURATION_KEY.fields_by_name['fragments']._serialized_options = b'\372B\005\222\001\002\010\001'
  _SCOPEDROUTECONFIGURATION.fields_by_name['name']._options = None
  _SCOPEDROUTECONFIGURATION.fields_by_name['name']._serialized_options = b'\372B\004r\002 \001'
  _SCOPEDROUTECONFIGURATION.fields_by_name['route_configuration_name']._options = None
  _SCOPEDROUTECONFIGURATION.fields_by_name['route_configuration_name']._serialized_options = b'\372B\004r\002 \001'
  _SCOPEDROUTECONFIGURATION.fields_by_name['key']._options = None
  _SCOPEDROUTECONFIGURATION.fields_by_name['key']._serialized_options = b'\372B\005\212\001\002\020\001'
  _globals['_SCOPEDROUTECONFIGURATION']._serialized_start=138
  _globals['_SCOPEDROUTECONFIGURATION']._serialized_end=434
  _globals['_SCOPEDROUTECONFIGURATION_KEY']._serialized_start=300
  _globals['_SCOPEDROUTECONFIGURATION_KEY']._serialized_end=434
  _globals['_SCOPEDROUTECONFIGURATION_KEY_FRAGMENT']._serialized_start=389
  _globals['_SCOPEDROUTECONFIGURATION_KEY_FRAGMENT']._serialized_end=434
# @@protoc_insertion_point(module_scope)
