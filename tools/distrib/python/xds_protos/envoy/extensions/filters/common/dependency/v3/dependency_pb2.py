# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: envoy/extensions/filters/common/dependency/v3/dependency.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


from udpa.annotations import status_pb2 as udpa_dot_annotations_dot_status__pb2
from validate import validate_pb2 as validate_dot_validate__pb2


DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n>envoy/extensions/filters/common/dependency/v3/dependency.proto\x12-envoy.extensions.filters.common.dependency.v3\x1a\x1dudpa/annotations/status.proto\x1a\x17validate/validate.proto\"\xc5\x01\n\nDependency\x12V\n\x04type\x18\x01 \x01(\x0e\x32H.envoy.extensions.filters.common.dependency.v3.Dependency.DependencyType\x12\x15\n\x04name\x18\x02 \x01(\tB\x07\xfa\x42\x04r\x02\x10\x01\"H\n\x0e\x44\x65pendencyType\x12\n\n\x06HEADER\x10\x00\x12\x14\n\x10\x46ILTER_STATE_KEY\x10\x01\x12\x14\n\x10\x44YNAMIC_METADATA\x10\x02\"\xe4\x02\n\x12\x46ilterDependencies\x12R\n\x0f\x64\x65\x63ode_required\x18\x01 \x03(\x0b\x32\x39.envoy.extensions.filters.common.dependency.v3.Dependency\x12R\n\x0f\x64\x65\x63ode_provided\x18\x02 \x03(\x0b\x32\x39.envoy.extensions.filters.common.dependency.v3.Dependency\x12R\n\x0f\x65ncode_required\x18\x03 \x03(\x0b\x32\x39.envoy.extensions.filters.common.dependency.v3.Dependency\x12R\n\x0f\x65ncode_provided\x18\x04 \x03(\x0b\x32\x39.envoy.extensions.filters.common.dependency.v3.Dependency\"\xb5\x01\n\x14MatchingRequirements\x12u\n\x15\x64\x61ta_input_allow_list\x18\x01 \x01(\x0b\x32V.envoy.extensions.filters.common.dependency.v3.MatchingRequirements.DataInputAllowList\x1a&\n\x12\x44\x61taInputAllowList\x12\x10\n\x08type_url\x18\x01 \x03(\tB\xbb\x01\n;io.envoyproxy.envoy.extensions.filters.common.dependency.v3B\x0f\x44\x65pendencyProtoP\x01Zagithub.com/envoyproxy/go-control-plane/envoy/extensions/filters/common/dependency/v3;dependencyv3\xba\x80\xc8\xd1\x06\x02\x10\x02\x62\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'envoy.extensions.filters.common.dependency.v3.dependency_pb2', _globals)
if _descriptor._USE_C_DESCRIPTORS == False:
  DESCRIPTOR._options = None
  DESCRIPTOR._serialized_options = b'\n;io.envoyproxy.envoy.extensions.filters.common.dependency.v3B\017DependencyProtoP\001Zagithub.com/envoyproxy/go-control-plane/envoy/extensions/filters/common/dependency/v3;dependencyv3\272\200\310\321\006\002\020\002'
  _DEPENDENCY.fields_by_name['name']._options = None
  _DEPENDENCY.fields_by_name['name']._serialized_options = b'\372B\004r\002\020\001'
  _globals['_DEPENDENCY']._serialized_start=170
  _globals['_DEPENDENCY']._serialized_end=367
  _globals['_DEPENDENCY_DEPENDENCYTYPE']._serialized_start=295
  _globals['_DEPENDENCY_DEPENDENCYTYPE']._serialized_end=367
  _globals['_FILTERDEPENDENCIES']._serialized_start=370
  _globals['_FILTERDEPENDENCIES']._serialized_end=726
  _globals['_MATCHINGREQUIREMENTS']._serialized_start=729
  _globals['_MATCHINGREQUIREMENTS']._serialized_end=910
  _globals['_MATCHINGREQUIREMENTS_DATAINPUTALLOWLIST']._serialized_start=872
  _globals['_MATCHINGREQUIREMENTS_DATAINPUTALLOWLIST']._serialized_end=910
# @@protoc_insertion_point(module_scope)
