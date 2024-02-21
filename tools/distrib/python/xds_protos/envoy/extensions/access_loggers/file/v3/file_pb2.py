# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: envoy/extensions/access_loggers/file/v3/file.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


from envoy.config.core.v3 import substitution_format_string_pb2 as envoy_dot_config_dot_core_dot_v3_dot_substitution__format__string__pb2
from google.protobuf import struct_pb2 as google_dot_protobuf_dot_struct__pb2
from envoy.annotations import deprecation_pb2 as envoy_dot_annotations_dot_deprecation__pb2
from udpa.annotations import status_pb2 as udpa_dot_annotations_dot_status__pb2
from udpa.annotations import versioning_pb2 as udpa_dot_annotations_dot_versioning__pb2
from validate import validate_pb2 as validate_dot_validate__pb2


DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n2envoy/extensions/access_loggers/file/v3/file.proto\x12\'envoy.extensions.access_loggers.file.v3\x1a\x35\x65nvoy/config/core/v3/substitution_format_string.proto\x1a\x1cgoogle/protobuf/struct.proto\x1a#envoy/annotations/deprecation.proto\x1a\x1dudpa/annotations/status.proto\x1a!udpa/annotations/versioning.proto\x1a\x17validate/validate.proto\"\xda\x02\n\rFileAccessLog\x12\x15\n\x04path\x18\x01 \x01(\tB\x07\xfa\x42\x04r\x02\x10\x01\x12\x1d\n\x06\x66ormat\x18\x02 \x01(\tB\x0b\x18\x01\x92\xc7\x86\xd8\x04\x03\x33.0H\x00\x12;\n\x0bjson_format\x18\x03 \x01(\x0b\x32\x17.google.protobuf.StructB\x0b\x18\x01\x92\xc7\x86\xd8\x04\x03\x33.0H\x00\x12\x41\n\x11typed_json_format\x18\x04 \x01(\x0b\x32\x17.google.protobuf.StructB\x0b\x18\x01\x92\xc7\x86\xd8\x04\x03\x33.0H\x00\x12N\n\nlog_format\x18\x05 \x01(\x0b\x32..envoy.config.core.v3.SubstitutionFormatStringB\x08\xfa\x42\x05\x8a\x01\x02\x10\x01H\x00:.\x9a\xc5\x88\x1e)\n\'envoy.config.accesslog.v2.FileAccessLogB\x13\n\x11\x61\x63\x63\x65ss_log_formatB\xa3\x01\n5io.envoyproxy.envoy.extensions.access_loggers.file.v3B\tFileProtoP\x01ZUgithub.com/envoyproxy/go-control-plane/envoy/extensions/access_loggers/file/v3;filev3\xba\x80\xc8\xd1\x06\x02\x10\x02\x62\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'envoy.extensions.access_loggers.file.v3.file_pb2', _globals)
if _descriptor._USE_C_DESCRIPTORS == False:
  DESCRIPTOR._options = None
  DESCRIPTOR._serialized_options = b'\n5io.envoyproxy.envoy.extensions.access_loggers.file.v3B\tFileProtoP\001ZUgithub.com/envoyproxy/go-control-plane/envoy/extensions/access_loggers/file/v3;filev3\272\200\310\321\006\002\020\002'
  _FILEACCESSLOG.fields_by_name['path']._options = None
  _FILEACCESSLOG.fields_by_name['path']._serialized_options = b'\372B\004r\002\020\001'
  _FILEACCESSLOG.fields_by_name['format']._options = None
  _FILEACCESSLOG.fields_by_name['format']._serialized_options = b'\030\001\222\307\206\330\004\0033.0'
  _FILEACCESSLOG.fields_by_name['json_format']._options = None
  _FILEACCESSLOG.fields_by_name['json_format']._serialized_options = b'\030\001\222\307\206\330\004\0033.0'
  _FILEACCESSLOG.fields_by_name['typed_json_format']._options = None
  _FILEACCESSLOG.fields_by_name['typed_json_format']._serialized_options = b'\030\001\222\307\206\330\004\0033.0'
  _FILEACCESSLOG.fields_by_name['log_format']._options = None
  _FILEACCESSLOG.fields_by_name['log_format']._serialized_options = b'\372B\005\212\001\002\020\001'
  _FILEACCESSLOG._options = None
  _FILEACCESSLOG._serialized_options = b'\232\305\210\036)\n\'envoy.config.accesslog.v2.FileAccessLog'
  _globals['_FILEACCESSLOG']._serialized_start=309
  _globals['_FILEACCESSLOG']._serialized_end=655
# @@protoc_insertion_point(module_scope)
