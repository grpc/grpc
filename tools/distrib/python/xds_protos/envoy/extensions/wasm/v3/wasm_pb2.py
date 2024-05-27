# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: envoy/extensions/wasm/v3/wasm.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


from envoy.config.core.v3 import base_pb2 as envoy_dot_config_dot_core_dot_v3_dot_base__pb2
from google.protobuf import any_pb2 as google_dot_protobuf_dot_any__pb2
from udpa.annotations import status_pb2 as udpa_dot_annotations_dot_status__pb2


DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n#envoy/extensions/wasm/v3/wasm.proto\x12\x18\x65nvoy.extensions.wasm.v3\x1a\x1f\x65nvoy/config/core/v3/base.proto\x1a\x19google/protobuf/any.proto\x1a\x1dudpa/annotations/status.proto\"\xf5\x01\n\x1b\x43\x61pabilityRestrictionConfig\x12l\n\x14\x61llowed_capabilities\x18\x01 \x03(\x0b\x32N.envoy.extensions.wasm.v3.CapabilityRestrictionConfig.AllowedCapabilitiesEntry\x1ah\n\x18\x41llowedCapabilitiesEntry\x12\x0b\n\x03key\x18\x01 \x01(\t\x12;\n\x05value\x18\x02 \x01(\x0b\x32,.envoy.extensions.wasm.v3.SanitizationConfig:\x02\x38\x01\"\x14\n\x12SanitizationConfig\"\x97\x02\n\x08VmConfig\x12\r\n\x05vm_id\x18\x01 \x01(\t\x12\x0f\n\x07runtime\x18\x02 \x01(\t\x12\x33\n\x04\x63ode\x18\x03 \x01(\x0b\x32%.envoy.config.core.v3.AsyncDataSource\x12+\n\rconfiguration\x18\x04 \x01(\x0b\x32\x14.google.protobuf.Any\x12\x19\n\x11\x61llow_precompiled\x18\x05 \x01(\x08\x12\x1f\n\x17nack_on_code_cache_miss\x18\x06 \x01(\x08\x12M\n\x15\x65nvironment_variables\x18\x07 \x01(\x0b\x32..envoy.extensions.wasm.v3.EnvironmentVariables\"\xb2\x01\n\x14\x45nvironmentVariables\x12\x15\n\rhost_env_keys\x18\x01 \x03(\t\x12Q\n\nkey_values\x18\x02 \x03(\x0b\x32=.envoy.extensions.wasm.v3.EnvironmentVariables.KeyValuesEntry\x1a\x30\n\x0eKeyValuesEntry\x12\x0b\n\x03key\x18\x01 \x01(\t\x12\r\n\x05value\x18\x02 \x01(\t:\x02\x38\x01\"\x8a\x02\n\x0cPluginConfig\x12\x0c\n\x04name\x18\x01 \x01(\t\x12\x0f\n\x07root_id\x18\x02 \x01(\t\x12\x37\n\tvm_config\x18\x03 \x01(\x0b\x32\".envoy.extensions.wasm.v3.VmConfigH\x00\x12+\n\rconfiguration\x18\x04 \x01(\x0b\x32\x14.google.protobuf.Any\x12\x11\n\tfail_open\x18\x05 \x01(\x08\x12\\\n\x1d\x63\x61pability_restriction_config\x18\x06 \x01(\x0b\x32\x35.envoy.extensions.wasm.v3.CapabilityRestrictionConfigB\x04\n\x02vm\"X\n\x0bWasmService\x12\x36\n\x06\x63onfig\x18\x01 \x01(\x0b\x32&.envoy.extensions.wasm.v3.PluginConfig\x12\x11\n\tsingleton\x18\x02 \x01(\x08\x42\x85\x01\n&io.envoyproxy.envoy.extensions.wasm.v3B\tWasmProtoP\x01ZFgithub.com/envoyproxy/go-control-plane/envoy/extensions/wasm/v3;wasmv3\xba\x80\xc8\xd1\x06\x02\x10\x02\x62\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'envoy.extensions.wasm.v3.wasm_pb2', _globals)
if _descriptor._USE_C_DESCRIPTORS == False:
  DESCRIPTOR._options = None
  DESCRIPTOR._serialized_options = b'\n&io.envoyproxy.envoy.extensions.wasm.v3B\tWasmProtoP\001ZFgithub.com/envoyproxy/go-control-plane/envoy/extensions/wasm/v3;wasmv3\272\200\310\321\006\002\020\002'
  _CAPABILITYRESTRICTIONCONFIG_ALLOWEDCAPABILITIESENTRY._options = None
  _CAPABILITYRESTRICTIONCONFIG_ALLOWEDCAPABILITIESENTRY._serialized_options = b'8\001'
  _ENVIRONMENTVARIABLES_KEYVALUESENTRY._options = None
  _ENVIRONMENTVARIABLES_KEYVALUESENTRY._serialized_options = b'8\001'
  _globals['_CAPABILITYRESTRICTIONCONFIG']._serialized_start=157
  _globals['_CAPABILITYRESTRICTIONCONFIG']._serialized_end=402
  _globals['_CAPABILITYRESTRICTIONCONFIG_ALLOWEDCAPABILITIESENTRY']._serialized_start=298
  _globals['_CAPABILITYRESTRICTIONCONFIG_ALLOWEDCAPABILITIESENTRY']._serialized_end=402
  _globals['_SANITIZATIONCONFIG']._serialized_start=404
  _globals['_SANITIZATIONCONFIG']._serialized_end=424
  _globals['_VMCONFIG']._serialized_start=427
  _globals['_VMCONFIG']._serialized_end=706
  _globals['_ENVIRONMENTVARIABLES']._serialized_start=709
  _globals['_ENVIRONMENTVARIABLES']._serialized_end=887
  _globals['_ENVIRONMENTVARIABLES_KEYVALUESENTRY']._serialized_start=839
  _globals['_ENVIRONMENTVARIABLES_KEYVALUESENTRY']._serialized_end=887
  _globals['_PLUGINCONFIG']._serialized_start=890
  _globals['_PLUGINCONFIG']._serialized_end=1156
  _globals['_WASMSERVICE']._serialized_start=1158
  _globals['_WASMSERVICE']._serialized_end=1246
# @@protoc_insertion_point(module_scope)
