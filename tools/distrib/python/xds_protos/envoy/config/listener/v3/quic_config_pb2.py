# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: envoy/config/listener/v3/quic_config.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


from envoy.config.core.v3 import base_pb2 as envoy_dot_config_dot_core_dot_v3_dot_base__pb2
from envoy.config.core.v3 import extension_pb2 as envoy_dot_config_dot_core_dot_v3_dot_extension__pb2
from envoy.config.core.v3 import protocol_pb2 as envoy_dot_config_dot_core_dot_v3_dot_protocol__pb2
from google.protobuf import duration_pb2 as google_dot_protobuf_dot_duration__pb2
from google.protobuf import wrappers_pb2 as google_dot_protobuf_dot_wrappers__pb2
from xds.annotations.v3 import status_pb2 as xds_dot_annotations_dot_v3_dot_status__pb2
from udpa.annotations import status_pb2 as udpa_dot_annotations_dot_status__pb2
from udpa.annotations import versioning_pb2 as udpa_dot_annotations_dot_versioning__pb2
from validate import validate_pb2 as validate_dot_validate__pb2


DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n*envoy/config/listener/v3/quic_config.proto\x12\x18\x65nvoy.config.listener.v3\x1a\x1f\x65nvoy/config/core/v3/base.proto\x1a$envoy/config/core/v3/extension.proto\x1a#envoy/config/core/v3/protocol.proto\x1a\x1egoogle/protobuf/duration.proto\x1a\x1egoogle/protobuf/wrappers.proto\x1a\x1fxds/annotations/v3/status.proto\x1a\x1dudpa/annotations/status.proto\x1a!udpa/annotations/versioning.proto\x1a\x17validate/validate.proto\"\xf2\x06\n\x13QuicProtocolOptions\x12H\n\x15quic_protocol_options\x18\x01 \x01(\x0b\x32).envoy.config.core.v3.QuicProtocolOptions\x12/\n\x0cidle_timeout\x18\x02 \x01(\x0b\x32\x19.google.protobuf.Duration\x12;\n\x18\x63rypto_handshake_timeout\x18\x03 \x01(\x0b\x32\x19.google.protobuf.Duration\x12\x39\n\x07\x65nabled\x18\x04 \x01(\x0b\x32(.envoy.config.core.v3.RuntimeFeatureFlag\x12X\n)packets_to_read_to_connection_count_ratio\x18\x05 \x01(\x0b\x32\x1c.google.protobuf.UInt32ValueB\x07\xfa\x42\x04*\x02(\x01\x12H\n\x14\x63rypto_stream_config\x18\x06 \x01(\x0b\x32*.envoy.config.core.v3.TypedExtensionConfig\x12G\n\x13proof_source_config\x18\x07 \x01(\x0b\x32*.envoy.config.core.v3.TypedExtensionConfig\x12R\n\x1e\x63onnection_id_generator_config\x18\x08 \x01(\x0b\x32*.envoy.config.core.v3.TypedExtensionConfig\x12]\n\x1fserver_preferred_address_config\x18\t \x01(\x0b\x32*.envoy.config.core.v3.TypedExtensionConfigB\x08\xd2\xc6\xa4\xe1\x06\x02\x08\x01\x12\x41\n\x1dsend_disable_active_migration\x18\n \x01(\x0b\x32\x1a.google.protobuf.BoolValue\x12S\n\x1f\x63onnection_debug_visitor_config\x18\x0b \x01(\x0b\x32*.envoy.config.core.v3.TypedExtensionConfig:0\x9a\xc5\x88\x1e+\n)envoy.api.v2.listener.QuicProtocolOptionsB\x8f\x01\n&io.envoyproxy.envoy.config.listener.v3B\x0fQuicConfigProtoP\x01ZJgithub.com/envoyproxy/go-control-plane/envoy/config/listener/v3;listenerv3\xba\x80\xc8\xd1\x06\x02\x10\x02\x62\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'envoy.config.listener.v3.quic_config_pb2', _globals)
if _descriptor._USE_C_DESCRIPTORS == False:
  DESCRIPTOR._options = None
  DESCRIPTOR._serialized_options = b'\n&io.envoyproxy.envoy.config.listener.v3B\017QuicConfigProtoP\001ZJgithub.com/envoyproxy/go-control-plane/envoy/config/listener/v3;listenerv3\272\200\310\321\006\002\020\002'
  _QUICPROTOCOLOPTIONS.fields_by_name['packets_to_read_to_connection_count_ratio']._options = None
  _QUICPROTOCOLOPTIONS.fields_by_name['packets_to_read_to_connection_count_ratio']._serialized_options = b'\372B\004*\002(\001'
  _QUICPROTOCOLOPTIONS.fields_by_name['server_preferred_address_config']._options = None
  _QUICPROTOCOLOPTIONS.fields_by_name['server_preferred_address_config']._serialized_options = b'\322\306\244\341\006\002\010\001'
  _QUICPROTOCOLOPTIONS._options = None
  _QUICPROTOCOLOPTIONS._serialized_options = b'\232\305\210\036+\n)envoy.api.v2.listener.QuicProtocolOptions'
  _globals['_QUICPROTOCOLOPTIONS']._serialized_start=369
  _globals['_QUICPROTOCOLOPTIONS']._serialized_end=1251
# @@protoc_insertion_point(module_scope)
