# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: opentelemetry/proto/trace/v1/trace.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


from opentelemetry.proto.common.v1 import common_pb2 as opentelemetry_dot_proto_dot_common_dot_v1_dot_common__pb2
from opentelemetry.proto.resource.v1 import resource_pb2 as opentelemetry_dot_proto_dot_resource_dot_v1_dot_resource__pb2


DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n(opentelemetry/proto/trace/v1/trace.proto\x12\x1copentelemetry.proto.trace.v1\x1a*opentelemetry/proto/common/v1/common.proto\x1a.opentelemetry/proto/resource/v1/resource.proto\"\xc2\x01\n\rResourceSpans\x12;\n\x08resource\x18\x01 \x01(\x0b\x32).opentelemetry.proto.resource.v1.Resource\x12`\n\x1dinstrumentation_library_spans\x18\x02 \x03(\x0b\x32\x39.opentelemetry.proto.trace.v1.InstrumentationLibrarySpans\x12\x12\n\nschema_url\x18\x03 \x01(\t\"\xbc\x01\n\x1bInstrumentationLibrarySpans\x12V\n\x17instrumentation_library\x18\x01 \x01(\x0b\x32\x35.opentelemetry.proto.common.v1.InstrumentationLibrary\x12\x31\n\x05spans\x18\x02 \x03(\x0b\x32\".opentelemetry.proto.trace.v1.Span\x12\x12\n\nschema_url\x18\x03 \x01(\t\"\xe6\x07\n\x04Span\x12\x10\n\x08trace_id\x18\x01 \x01(\x0c\x12\x0f\n\x07span_id\x18\x02 \x01(\x0c\x12\x13\n\x0btrace_state\x18\x03 \x01(\t\x12\x16\n\x0eparent_span_id\x18\x04 \x01(\x0c\x12\x0c\n\x04name\x18\x05 \x01(\t\x12\x39\n\x04kind\x18\x06 \x01(\x0e\x32+.opentelemetry.proto.trace.v1.Span.SpanKind\x12\x1c\n\x14start_time_unix_nano\x18\x07 \x01(\x06\x12\x1a\n\x12\x65nd_time_unix_nano\x18\x08 \x01(\x06\x12;\n\nattributes\x18\t \x03(\x0b\x32\'.opentelemetry.proto.common.v1.KeyValue\x12 \n\x18\x64ropped_attributes_count\x18\n \x01(\r\x12\x38\n\x06\x65vents\x18\x0b \x03(\x0b\x32(.opentelemetry.proto.trace.v1.Span.Event\x12\x1c\n\x14\x64ropped_events_count\x18\x0c \x01(\r\x12\x36\n\x05links\x18\r \x03(\x0b\x32\'.opentelemetry.proto.trace.v1.Span.Link\x12\x1b\n\x13\x64ropped_links_count\x18\x0e \x01(\r\x12\x34\n\x06status\x18\x0f \x01(\x0b\x32$.opentelemetry.proto.trace.v1.Status\x1a\x8c\x01\n\x05\x45vent\x12\x16\n\x0etime_unix_nano\x18\x01 \x01(\x06\x12\x0c\n\x04name\x18\x02 \x01(\t\x12;\n\nattributes\x18\x03 \x03(\x0b\x32\'.opentelemetry.proto.common.v1.KeyValue\x12 \n\x18\x64ropped_attributes_count\x18\x04 \x01(\r\x1a\x9d\x01\n\x04Link\x12\x10\n\x08trace_id\x18\x01 \x01(\x0c\x12\x0f\n\x07span_id\x18\x02 \x01(\x0c\x12\x13\n\x0btrace_state\x18\x03 \x01(\t\x12;\n\nattributes\x18\x04 \x03(\x0b\x32\'.opentelemetry.proto.common.v1.KeyValue\x12 \n\x18\x64ropped_attributes_count\x18\x05 \x01(\r\"\x99\x01\n\x08SpanKind\x12\x19\n\x15SPAN_KIND_UNSPECIFIED\x10\x00\x12\x16\n\x12SPAN_KIND_INTERNAL\x10\x01\x12\x14\n\x10SPAN_KIND_SERVER\x10\x02\x12\x14\n\x10SPAN_KIND_CLIENT\x10\x03\x12\x16\n\x12SPAN_KIND_PRODUCER\x10\x04\x12\x16\n\x12SPAN_KIND_CONSUMER\x10\x05\"\xdd\x07\n\x06Status\x12V\n\x0f\x64\x65precated_code\x18\x01 \x01(\x0e\x32\x39.opentelemetry.proto.trace.v1.Status.DeprecatedStatusCodeB\x02\x18\x01\x12\x0f\n\x07message\x18\x02 \x01(\t\x12=\n\x04\x63ode\x18\x03 \x01(\x0e\x32/.opentelemetry.proto.trace.v1.Status.StatusCode\"\xda\x05\n\x14\x44\x65precatedStatusCode\x12\x1d\n\x19\x44\x45PRECATED_STATUS_CODE_OK\x10\x00\x12$\n DEPRECATED_STATUS_CODE_CANCELLED\x10\x01\x12(\n$DEPRECATED_STATUS_CODE_UNKNOWN_ERROR\x10\x02\x12+\n\'DEPRECATED_STATUS_CODE_INVALID_ARGUMENT\x10\x03\x12,\n(DEPRECATED_STATUS_CODE_DEADLINE_EXCEEDED\x10\x04\x12$\n DEPRECATED_STATUS_CODE_NOT_FOUND\x10\x05\x12)\n%DEPRECATED_STATUS_CODE_ALREADY_EXISTS\x10\x06\x12,\n(DEPRECATED_STATUS_CODE_PERMISSION_DENIED\x10\x07\x12-\n)DEPRECATED_STATUS_CODE_RESOURCE_EXHAUSTED\x10\x08\x12.\n*DEPRECATED_STATUS_CODE_FAILED_PRECONDITION\x10\t\x12\"\n\x1e\x44\x45PRECATED_STATUS_CODE_ABORTED\x10\n\x12\'\n#DEPRECATED_STATUS_CODE_OUT_OF_RANGE\x10\x0b\x12(\n$DEPRECATED_STATUS_CODE_UNIMPLEMENTED\x10\x0c\x12)\n%DEPRECATED_STATUS_CODE_INTERNAL_ERROR\x10\r\x12&\n\"DEPRECATED_STATUS_CODE_UNAVAILABLE\x10\x0e\x12$\n DEPRECATED_STATUS_CODE_DATA_LOSS\x10\x0f\x12*\n&DEPRECATED_STATUS_CODE_UNAUTHENTICATED\x10\x10\"N\n\nStatusCode\x12\x15\n\x11STATUS_CODE_UNSET\x10\x00\x12\x12\n\x0eSTATUS_CODE_OK\x10\x01\x12\x15\n\x11STATUS_CODE_ERROR\x10\x02\x42n\n\x1fio.opentelemetry.proto.trace.v1B\nTraceProtoP\x01Z=github.com/open-telemetry/opentelemetry-proto/gen/go/trace/v1b\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'opentelemetry.proto.trace.v1.trace_pb2', _globals)
if _descriptor._USE_C_DESCRIPTORS == False:
  DESCRIPTOR._options = None
  DESCRIPTOR._serialized_options = b'\n\037io.opentelemetry.proto.trace.v1B\nTraceProtoP\001Z=github.com/open-telemetry/opentelemetry-proto/gen/go/trace/v1'
  _STATUS.fields_by_name['deprecated_code']._options = None
  _STATUS.fields_by_name['deprecated_code']._serialized_options = b'\030\001'
  _globals['_RESOURCESPANS']._serialized_start=167
  _globals['_RESOURCESPANS']._serialized_end=361
  _globals['_INSTRUMENTATIONLIBRARYSPANS']._serialized_start=364
  _globals['_INSTRUMENTATIONLIBRARYSPANS']._serialized_end=552
  _globals['_SPAN']._serialized_start=555
  _globals['_SPAN']._serialized_end=1553
  _globals['_SPAN_EVENT']._serialized_start=1097
  _globals['_SPAN_EVENT']._serialized_end=1237
  _globals['_SPAN_LINK']._serialized_start=1240
  _globals['_SPAN_LINK']._serialized_end=1397
  _globals['_SPAN_SPANKIND']._serialized_start=1400
  _globals['_SPAN_SPANKIND']._serialized_end=1553
  _globals['_STATUS']._serialized_start=1556
  _globals['_STATUS']._serialized_end=2545
  _globals['_STATUS_DEPRECATEDSTATUSCODE']._serialized_start=1735
  _globals['_STATUS_DEPRECATEDSTATUSCODE']._serialized_end=2465
  _globals['_STATUS_STATUSCODE']._serialized_start=2467
  _globals['_STATUS_STATUSCODE']._serialized_end=2545
# @@protoc_insertion_point(module_scope)
