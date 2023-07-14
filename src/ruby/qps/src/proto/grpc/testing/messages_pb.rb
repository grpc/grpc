# frozen_string_literal: true
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: src/proto/grpc/testing/messages.proto

require 'google/protobuf'


descriptor_data = "\n%src/proto/grpc/testing/messages.proto\x12\x0cgrpc.testing\"\x1a\n\tBoolValue\x12\r\n\x05value\x18\x01 \x01(\x08\"@\n\x07Payload\x12\'\n\x04type\x18\x01 \x01(\x0e\x32\x19.grpc.testing.PayloadType\x12\x0c\n\x04\x62ody\x18\x02 \x01(\x0c\"+\n\nEchoStatus\x12\x0c\n\x04\x63ode\x18\x01 \x01(\x05\x12\x0f\n\x07message\x18\x02 \x01(\t\"\xfa\x03\n\rSimpleRequest\x12\x30\n\rresponse_type\x18\x01 \x01(\x0e\x32\x19.grpc.testing.PayloadType\x12\x15\n\rresponse_size\x18\x02 \x01(\x05\x12&\n\x07payload\x18\x03 \x01(\x0b\x32\x15.grpc.testing.Payload\x12\x15\n\rfill_username\x18\x04 \x01(\x08\x12\x18\n\x10\x66ill_oauth_scope\x18\x05 \x01(\x08\x12\x34\n\x13response_compressed\x18\x06 \x01(\x0b\x32\x17.grpc.testing.BoolValue\x12\x31\n\x0fresponse_status\x18\x07 \x01(\x0b\x32\x18.grpc.testing.EchoStatus\x12\x32\n\x11\x65xpect_compressed\x18\x08 \x01(\x0b\x32\x17.grpc.testing.BoolValue\x12\x16\n\x0e\x66ill_server_id\x18\t \x01(\x08\x12\x1e\n\x16\x66ill_grpclb_route_type\x18\n \x01(\x08\x12;\n\x15orca_per_query_report\x18\x0b \x01(\x0b\x32\x1c.grpc.testing.TestOrcaReport\x12\x35\n\x0forca_oob_report\x18\x0c \x01(\x0b\x32\x1c.grpc.testing.TestOrcaReport\"\xbe\x01\n\x0eSimpleResponse\x12&\n\x07payload\x18\x01 \x01(\x0b\x32\x15.grpc.testing.Payload\x12\x10\n\x08username\x18\x02 \x01(\t\x12\x13\n\x0boauth_scope\x18\x03 \x01(\t\x12\x11\n\tserver_id\x18\x04 \x01(\t\x12\x38\n\x11grpclb_route_type\x18\x05 \x01(\x0e\x32\x1d.grpc.testing.GrpclbRouteType\x12\x10\n\x08hostname\x18\x06 \x01(\t\"w\n\x19StreamingInputCallRequest\x12&\n\x07payload\x18\x01 \x01(\x0b\x32\x15.grpc.testing.Payload\x12\x32\n\x11\x65xpect_compressed\x18\x02 \x01(\x0b\x32\x17.grpc.testing.BoolValue\"=\n\x1aStreamingInputCallResponse\x12\x1f\n\x17\x61ggregated_payload_size\x18\x01 \x01(\x05\"d\n\x12ResponseParameters\x12\x0c\n\x04size\x18\x01 \x01(\x05\x12\x13\n\x0binterval_us\x18\x02 \x01(\x05\x12+\n\ncompressed\x18\x03 \x01(\x0b\x32\x17.grpc.testing.BoolValue\"\x9f\x02\n\x1aStreamingOutputCallRequest\x12\x30\n\rresponse_type\x18\x01 \x01(\x0e\x32\x19.grpc.testing.PayloadType\x12=\n\x13response_parameters\x18\x02 \x03(\x0b\x32 .grpc.testing.ResponseParameters\x12&\n\x07payload\x18\x03 \x01(\x0b\x32\x15.grpc.testing.Payload\x12\x31\n\x0fresponse_status\x18\x07 \x01(\x0b\x32\x18.grpc.testing.EchoStatus\x12\x35\n\x0forca_oob_report\x18\x08 \x01(\x0b\x32\x1c.grpc.testing.TestOrcaReport\"E\n\x1bStreamingOutputCallResponse\x12&\n\x07payload\x18\x01 \x01(\x0b\x32\x15.grpc.testing.Payload\"3\n\x0fReconnectParams\x12 \n\x18max_reconnect_backoff_ms\x18\x01 \x01(\x05\"3\n\rReconnectInfo\x12\x0e\n\x06passed\x18\x01 \x01(\x08\x12\x12\n\nbackoff_ms\x18\x02 \x03(\x05\"A\n\x18LoadBalancerStatsRequest\x12\x10\n\x08num_rpcs\x18\x01 \x01(\x05\x12\x13\n\x0btimeout_sec\x18\x02 \x01(\x05\"\x8b\x04\n\x19LoadBalancerStatsResponse\x12M\n\x0crpcs_by_peer\x18\x01 \x03(\x0b\x32\x37.grpc.testing.LoadBalancerStatsResponse.RpcsByPeerEntry\x12\x14\n\x0cnum_failures\x18\x02 \x01(\x05\x12Q\n\x0erpcs_by_method\x18\x03 \x03(\x0b\x32\x39.grpc.testing.LoadBalancerStatsResponse.RpcsByMethodEntry\x1a\x99\x01\n\nRpcsByPeer\x12X\n\x0crpcs_by_peer\x18\x01 \x03(\x0b\x32\x42.grpc.testing.LoadBalancerStatsResponse.RpcsByPeer.RpcsByPeerEntry\x1a\x31\n\x0fRpcsByPeerEntry\x12\x0b\n\x03key\x18\x01 \x01(\t\x12\r\n\x05value\x18\x02 \x01(\x05:\x02\x38\x01\x1a\x31\n\x0fRpcsByPeerEntry\x12\x0b\n\x03key\x18\x01 \x01(\t\x12\r\n\x05value\x18\x02 \x01(\x05:\x02\x38\x01\x1ag\n\x11RpcsByMethodEntry\x12\x0b\n\x03key\x18\x01 \x01(\t\x12\x41\n\x05value\x18\x02 \x01(\x0b\x32\x32.grpc.testing.LoadBalancerStatsResponse.RpcsByPeer:\x02\x38\x01\"%\n#LoadBalancerAccumulatedStatsRequest\"\xd8\x07\n$LoadBalancerAccumulatedStatsResponse\x12v\n\x1anum_rpcs_started_by_method\x18\x01 \x03(\x0b\x32N.grpc.testing.LoadBalancerAccumulatedStatsResponse.NumRpcsStartedByMethodEntryB\x02\x18\x01\x12z\n\x1cnum_rpcs_succeeded_by_method\x18\x02 \x03(\x0b\x32P.grpc.testing.LoadBalancerAccumulatedStatsResponse.NumRpcsSucceededByMethodEntryB\x02\x18\x01\x12t\n\x19num_rpcs_failed_by_method\x18\x03 \x03(\x0b\x32M.grpc.testing.LoadBalancerAccumulatedStatsResponse.NumRpcsFailedByMethodEntryB\x02\x18\x01\x12`\n\x10stats_per_method\x18\x04 \x03(\x0b\x32\x46.grpc.testing.LoadBalancerAccumulatedStatsResponse.StatsPerMethodEntry\x1a=\n\x1bNumRpcsStartedByMethodEntry\x12\x0b\n\x03key\x18\x01 \x01(\t\x12\r\n\x05value\x18\x02 \x01(\x05:\x02\x38\x01\x1a?\n\x1dNumRpcsSucceededByMethodEntry\x12\x0b\n\x03key\x18\x01 \x01(\t\x12\r\n\x05value\x18\x02 \x01(\x05:\x02\x38\x01\x1a<\n\x1aNumRpcsFailedByMethodEntry\x12\x0b\n\x03key\x18\x01 \x01(\t\x12\r\n\x05value\x18\x02 \x01(\x05:\x02\x38\x01\x1a\xae\x01\n\x0bMethodStats\x12\x14\n\x0crpcs_started\x18\x01 \x01(\x05\x12Z\n\x06result\x18\x02 \x03(\x0b\x32J.grpc.testing.LoadBalancerAccumulatedStatsResponse.MethodStats.ResultEntry\x1a-\n\x0bResultEntry\x12\x0b\n\x03key\x18\x01 \x01(\x05\x12\r\n\x05value\x18\x02 \x01(\x05:\x02\x38\x01\x1au\n\x13StatsPerMethodEntry\x12\x0b\n\x03key\x18\x01 \x01(\t\x12M\n\x05value\x18\x02 \x01(\x0b\x32>.grpc.testing.LoadBalancerAccumulatedStatsResponse.MethodStats:\x02\x38\x01\"\xba\x02\n\x16\x43lientConfigureRequest\x12;\n\x05types\x18\x01 \x03(\x0e\x32,.grpc.testing.ClientConfigureRequest.RpcType\x12?\n\x08metadata\x18\x02 \x03(\x0b\x32-.grpc.testing.ClientConfigureRequest.Metadata\x12\x13\n\x0btimeout_sec\x18\x03 \x01(\x05\x1a\x62\n\x08Metadata\x12:\n\x04type\x18\x01 \x01(\x0e\x32,.grpc.testing.ClientConfigureRequest.RpcType\x12\x0b\n\x03key\x18\x02 \x01(\t\x12\r\n\x05value\x18\x03 \x01(\t\")\n\x07RpcType\x12\x0e\n\nEMPTY_CALL\x10\x00\x12\x0e\n\nUNARY_CALL\x10\x01\"\x19\n\x17\x43lientConfigureResponse\"\x19\n\nMemorySize\x12\x0b\n\x03rss\x18\x01 \x01(\x03\"\xb6\x02\n\x0eTestOrcaReport\x12\x17\n\x0f\x63pu_utilization\x18\x01 \x01(\x01\x12\x1a\n\x12memory_utilization\x18\x02 \x01(\x01\x12\x43\n\x0crequest_cost\x18\x03 \x03(\x0b\x32-.grpc.testing.TestOrcaReport.RequestCostEntry\x12\x42\n\x0butilization\x18\x04 \x03(\x0b\x32-.grpc.testing.TestOrcaReport.UtilizationEntry\x1a\x32\n\x10RequestCostEntry\x12\x0b\n\x03key\x18\x01 \x01(\t\x12\r\n\x05value\x18\x02 \x01(\x01:\x02\x38\x01\x1a\x32\n\x10UtilizationEntry\x12\x0b\n\x03key\x18\x01 \x01(\t\x12\r\n\x05value\x18\x02 \x01(\x01:\x02\x38\x01*\x1f\n\x0bPayloadType\x12\x10\n\x0c\x43OMPRESSABLE\x10\x00*o\n\x0fGrpclbRouteType\x12\x1d\n\x19GRPCLB_ROUTE_TYPE_UNKNOWN\x10\x00\x12\x1e\n\x1aGRPCLB_ROUTE_TYPE_FALLBACK\x10\x01\x12\x1d\n\x19GRPCLB_ROUTE_TYPE_BACKEND\x10\x02\x42\x1d\n\x1bio.grpc.testing.integrationb\x06proto3"

pool = Google::Protobuf::DescriptorPool.generated_pool

begin
  pool.add_serialized_file(descriptor_data)
rescue TypeError => e
  # Compatibility code: will be removed in the next major version.
  require 'google/protobuf/descriptor_pb'
  parsed = Google::Protobuf::FileDescriptorProto.decode(descriptor_data)
  parsed.clear_dependency
  serialized = parsed.class.encode(parsed)
  file = pool.add_serialized_file(serialized)
  warn "Warning: Protobuf detected an import path issue while loading generated file #{__FILE__}"
  imports = [
  ]
  imports.each do |type_name, expected_filename|
    import_file = pool.lookup(type_name).file_descriptor
    if import_file.name != expected_filename
      warn "- #{file.name} imports #{expected_filename}, but that import was loaded as #{import_file.name}"
    end
  end
  warn "Each proto file must use a consistent fully-qualified name."
  warn "This will become an error in the next major version."
end

module Grpc
  module Testing
    BoolValue = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.BoolValue").msgclass
    Payload = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.Payload").msgclass
    EchoStatus = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.EchoStatus").msgclass
    SimpleRequest = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.SimpleRequest").msgclass
    SimpleResponse = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.SimpleResponse").msgclass
    StreamingInputCallRequest = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.StreamingInputCallRequest").msgclass
    StreamingInputCallResponse = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.StreamingInputCallResponse").msgclass
    ResponseParameters = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ResponseParameters").msgclass
    StreamingOutputCallRequest = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.StreamingOutputCallRequest").msgclass
    StreamingOutputCallResponse = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.StreamingOutputCallResponse").msgclass
    ReconnectParams = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ReconnectParams").msgclass
    ReconnectInfo = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ReconnectInfo").msgclass
    LoadBalancerStatsRequest = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.LoadBalancerStatsRequest").msgclass
    LoadBalancerStatsResponse = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.LoadBalancerStatsResponse").msgclass
    LoadBalancerStatsResponse::RpcsByPeer = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.LoadBalancerStatsResponse.RpcsByPeer").msgclass
    LoadBalancerAccumulatedStatsRequest = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.LoadBalancerAccumulatedStatsRequest").msgclass
    LoadBalancerAccumulatedStatsResponse = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.LoadBalancerAccumulatedStatsResponse").msgclass
    LoadBalancerAccumulatedStatsResponse::MethodStats = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.LoadBalancerAccumulatedStatsResponse.MethodStats").msgclass
    ClientConfigureRequest = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ClientConfigureRequest").msgclass
    ClientConfigureRequest::Metadata = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ClientConfigureRequest.Metadata").msgclass
    ClientConfigureRequest::RpcType = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ClientConfigureRequest.RpcType").enummodule
    ClientConfigureResponse = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ClientConfigureResponse").msgclass
    MemorySize = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.MemorySize").msgclass
    TestOrcaReport = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.TestOrcaReport").msgclass
    PayloadType = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.PayloadType").enummodule
    GrpclbRouteType = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.GrpclbRouteType").enummodule
  end
end
