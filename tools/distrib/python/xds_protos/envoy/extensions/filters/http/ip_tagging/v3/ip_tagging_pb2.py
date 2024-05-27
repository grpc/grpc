# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


from envoy.config.core.v3 import address_pb2 as envoy_dot_config_dot_core_dot_v3_dot_address__pb2
from udpa.annotations import status_pb2 as udpa_dot_annotations_dot_status__pb2
from udpa.annotations import versioning_pb2 as udpa_dot_annotations_dot_versioning__pb2
from validate import validate_pb2 as validate_dot_validate__pb2


DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n<envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.proto\x12+envoy.extensions.filters.http.ip_tagging.v3\x1a\"envoy/config/core/v3/address.proto\x1a\x1dudpa/annotations/status.proto\x1a!udpa/annotations/versioning.proto\x1a\x17validate/validate.proto\"\xc6\x03\n\tIPTagging\x12\x62\n\x0crequest_type\x18\x01 \x01(\x0e\x32\x42.envoy.extensions.filters.http.ip_tagging.v3.IPTagging.RequestTypeB\x08\xfa\x42\x05\x82\x01\x02\x10\x01\x12W\n\x07ip_tags\x18\x04 \x03(\x0b\x32<.envoy.extensions.filters.http.ip_tagging.v3.IPTagging.IPTagB\x08\xfa\x42\x05\x92\x01\x02\x08\x01\x1a\x8d\x01\n\x05IPTag\x12\x13\n\x0bip_tag_name\x18\x01 \x01(\t\x12\x30\n\x07ip_list\x18\x02 \x03(\x0b\x32\x1f.envoy.config.core.v3.CidrRange:=\x9a\xc5\x88\x1e\x38\n6envoy.config.filter.http.ip_tagging.v2.IPTagging.IPTag\"3\n\x0bRequestType\x12\x08\n\x04\x42OTH\x10\x00\x12\x0c\n\x08INTERNAL\x10\x01\x12\x0c\n\x08\x45XTERNAL\x10\x02:7\x9a\xc5\x88\x1e\x32\n0envoy.config.filter.http.ip_tagging.v2.IPTaggingB\xb6\x01\n9io.envoyproxy.envoy.extensions.filters.http.ip_tagging.v3B\x0eIpTaggingProtoP\x01Z_github.com/envoyproxy/go-control-plane/envoy/extensions/filters/http/ip_tagging/v3;ip_taggingv3\xba\x80\xc8\xd1\x06\x02\x10\x02\x62\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'envoy.extensions.filters.http.ip_tagging.v3.ip_tagging_pb2', _globals)
if _descriptor._USE_C_DESCRIPTORS == False:
  DESCRIPTOR._options = None
  DESCRIPTOR._serialized_options = b'\n9io.envoyproxy.envoy.extensions.filters.http.ip_tagging.v3B\016IpTaggingProtoP\001Z_github.com/envoyproxy/go-control-plane/envoy/extensions/filters/http/ip_tagging/v3;ip_taggingv3\272\200\310\321\006\002\020\002'
  _IPTAGGING_IPTAG._options = None
  _IPTAGGING_IPTAG._serialized_options = b'\232\305\210\0368\n6envoy.config.filter.http.ip_tagging.v2.IPTagging.IPTag'
  _IPTAGGING.fields_by_name['request_type']._options = None
  _IPTAGGING.fields_by_name['request_type']._serialized_options = b'\372B\005\202\001\002\020\001'
  _IPTAGGING.fields_by_name['ip_tags']._options = None
  _IPTAGGING.fields_by_name['ip_tags']._serialized_options = b'\372B\005\222\001\002\010\001'
  _IPTAGGING._options = None
  _IPTAGGING._serialized_options = b'\232\305\210\0362\n0envoy.config.filter.http.ip_tagging.v2.IPTagging'
  _globals['_IPTAGGING']._serialized_start=237
  _globals['_IPTAGGING']._serialized_end=691
  _globals['_IPTAGGING_IPTAG']._serialized_start=440
  _globals['_IPTAGGING_IPTAG']._serialized_end=581
  _globals['_IPTAGGING_REQUESTTYPE']._serialized_start=583
  _globals['_IPTAGGING_REQUESTTYPE']._serialized_end=634
# @@protoc_insertion_point(module_scope)
