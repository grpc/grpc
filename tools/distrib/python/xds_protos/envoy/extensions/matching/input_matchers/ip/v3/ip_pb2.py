# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: envoy/extensions/matching/input_matchers/ip/v3/ip.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


from envoy.config.core.v3 import address_pb2 as envoy_dot_config_dot_core_dot_v3_dot_address__pb2
from udpa.annotations import status_pb2 as udpa_dot_annotations_dot_status__pb2
from validate import validate_pb2 as validate_dot_validate__pb2


DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n7envoy/extensions/matching/input_matchers/ip/v3/ip.proto\x12.envoy.extensions.matching.input_matchers.ip.v3\x1a\"envoy/config/core/v3/address.proto\x1a\x1dudpa/annotations/status.proto\x1a\x17validate/validate.proto\"b\n\x02Ip\x12>\n\x0b\x63idr_ranges\x18\x01 \x03(\x0b\x32\x1f.envoy.config.core.v3.CidrRangeB\x08\xfa\x42\x05\x92\x01\x02\x08\x01\x12\x1c\n\x0bstat_prefix\x18\x02 \x01(\tB\x07\xfa\x42\x04r\x02\x10\x01\x42\xad\x01\n<io.envoyproxy.envoy.extensions.matching.input_matchers.ip.v3B\x07IpProtoP\x01ZZgithub.com/envoyproxy/go-control-plane/envoy/extensions/matching/input_matchers/ip/v3;ipv3\xba\x80\xc8\xd1\x06\x02\x10\x02\x62\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'envoy.extensions.matching.input_matchers.ip.v3.ip_pb2', _globals)
if _descriptor._USE_C_DESCRIPTORS == False:
  DESCRIPTOR._options = None
  DESCRIPTOR._serialized_options = b'\n<io.envoyproxy.envoy.extensions.matching.input_matchers.ip.v3B\007IpProtoP\001ZZgithub.com/envoyproxy/go-control-plane/envoy/extensions/matching/input_matchers/ip/v3;ipv3\272\200\310\321\006\002\020\002'
  _IP.fields_by_name['cidr_ranges']._options = None
  _IP.fields_by_name['cidr_ranges']._serialized_options = b'\372B\005\222\001\002\010\001'
  _IP.fields_by_name['stat_prefix']._options = None
  _IP.fields_by_name['stat_prefix']._serialized_options = b'\372B\004r\002\020\001'
  _globals['_IP']._serialized_start=199
  _globals['_IP']._serialized_end=297
# @@protoc_insertion_point(module_scope)
