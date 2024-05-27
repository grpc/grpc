# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: envoy/service/extension/v3/config_discovery.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


from envoy.service.discovery.v3 import discovery_pb2 as envoy_dot_service_dot_discovery_dot_v3_dot_discovery__pb2
from google.api import annotations_pb2 as google_dot_api_dot_annotations__pb2
from envoy.annotations import resource_pb2 as envoy_dot_annotations_dot_resource__pb2
from udpa.annotations import status_pb2 as udpa_dot_annotations_dot_status__pb2


DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n1envoy/service/extension/v3/config_discovery.proto\x12\x1a\x65nvoy.service.extension.v3\x1a*envoy/service/discovery/v3/discovery.proto\x1a\x1cgoogle/api/annotations.proto\x1a envoy/annotations/resource.proto\x1a\x1dudpa/annotations/status.proto\"\x0b\n\tEcdsDummy2\xfb\x03\n\x1f\x45xtensionConfigDiscoveryService\x12{\n\x16StreamExtensionConfigs\x12,.envoy.service.discovery.v3.DiscoveryRequest\x1a-.envoy.service.discovery.v3.DiscoveryResponse\"\x00(\x01\x30\x01\x12\x84\x01\n\x15\x44\x65ltaExtensionConfigs\x12\x31.envoy.service.discovery.v3.DeltaDiscoveryRequest\x1a\x32.envoy.service.discovery.v3.DeltaDiscoveryResponse\"\x00(\x01\x30\x01\x12\xa0\x01\n\x15\x46\x65tchExtensionConfigs\x12,.envoy.service.discovery.v3.DiscoveryRequest\x1a-.envoy.service.discovery.v3.DiscoveryResponse\"*\x82\xd3\xe4\x93\x02$\"\x1f/v3/discovery:extension_configs:\x01*\x1a\x31\x8a\xa4\x96\xf3\x07+\n)envoy.config.core.v3.TypedExtensionConfigB\x99\x01\n(io.envoyproxy.envoy.service.extension.v3B\x14\x43onfigDiscoveryProtoP\x01ZMgithub.com/envoyproxy/go-control-plane/envoy/service/extension/v3;extensionv3\xba\x80\xc8\xd1\x06\x02\x10\x02\x62\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'envoy.service.extension.v3.config_discovery_pb2', _globals)
if _descriptor._USE_C_DESCRIPTORS == False:
  DESCRIPTOR._options = None
  DESCRIPTOR._serialized_options = b'\n(io.envoyproxy.envoy.service.extension.v3B\024ConfigDiscoveryProtoP\001ZMgithub.com/envoyproxy/go-control-plane/envoy/service/extension/v3;extensionv3\272\200\310\321\006\002\020\002'
  _EXTENSIONCONFIGDISCOVERYSERVICE._options = None
  _EXTENSIONCONFIGDISCOVERYSERVICE._serialized_options = b'\212\244\226\363\007+\n)envoy.config.core.v3.TypedExtensionConfig'
  _EXTENSIONCONFIGDISCOVERYSERVICE.methods_by_name['FetchExtensionConfigs']._options = None
  _EXTENSIONCONFIGDISCOVERYSERVICE.methods_by_name['FetchExtensionConfigs']._serialized_options = b'\202\323\344\223\002$\"\037/v3/discovery:extension_configs:\001*'
  _globals['_ECDSDUMMY']._serialized_start=220
  _globals['_ECDSDUMMY']._serialized_end=231
  _globals['_EXTENSIONCONFIGDISCOVERYSERVICE']._serialized_start=234
  _globals['_EXTENSIONCONFIGDISCOVERYSERVICE']._serialized_end=741
# @@protoc_insertion_point(module_scope)
