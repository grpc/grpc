# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: flow_control.proto
# Protobuf Python Version: 5.26.1
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()




DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n\x12\x66low_control.proto\x12\x0b\x66lowcontrol\"\x1a\n\x07Request\x12\x0f\n\x07message\x18\x01 \x01(\t\"\x18\n\x05Reply\x12\x0f\n\x07message\x18\x01 \x01(\t2R\n\x0b\x46lowControl\x12\x43\n\x11\x42idiStreamingCall\x12\x14.flowcontrol.Request\x1a\x12.flowcontrol.Reply\"\x00(\x01\x30\x01\x62\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'flow_control_pb2', _globals)
if not _descriptor._USE_C_DESCRIPTORS:
  DESCRIPTOR._loaded_options = None
  _globals['_REQUEST']._serialized_start=35
  _globals['_REQUEST']._serialized_end=61
  _globals['_REPLY']._serialized_start=63
  _globals['_REPLY']._serialized_end=87
  _globals['_FLOWCONTROL']._serialized_start=89
  _globals['_FLOWCONTROL']._serialized_end=171
# @@protoc_insertion_point(module_scope)
