# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: src/proto/grpc/testing/messages.proto

require 'google/protobuf'

Google::Protobuf::DescriptorPool.generated_pool.build do
  add_message "grpc.testing.Payload" do
    optional :type, :enum, 1, "grpc.testing.PayloadType"
    optional :body, :bytes, 2
  end
  add_message "grpc.testing.EchoStatus" do
    optional :code, :int32, 1
    optional :message, :string, 2
  end
  add_message "grpc.testing.SimpleRequest" do
    optional :response_type, :enum, 1, "grpc.testing.PayloadType"
    optional :response_size, :int32, 2
    optional :payload, :message, 3, "grpc.testing.Payload"
    optional :fill_username, :bool, 4
    optional :fill_oauth_scope, :bool, 5
    optional :response_compression, :enum, 6, "grpc.testing.CompressionType"
    optional :response_status, :message, 7, "grpc.testing.EchoStatus"
  end
  add_message "grpc.testing.SimpleResponse" do
    optional :payload, :message, 1, "grpc.testing.Payload"
    optional :username, :string, 2
    optional :oauth_scope, :string, 3
  end
  add_message "grpc.testing.StreamingInputCallRequest" do
    optional :payload, :message, 1, "grpc.testing.Payload"
  end
  add_message "grpc.testing.StreamingInputCallResponse" do
    optional :aggregated_payload_size, :int32, 1
  end
  add_message "grpc.testing.ResponseParameters" do
    optional :size, :int32, 1
    optional :interval_us, :int32, 2
  end
  add_message "grpc.testing.StreamingOutputCallRequest" do
    optional :response_type, :enum, 1, "grpc.testing.PayloadType"
    repeated :response_parameters, :message, 2, "grpc.testing.ResponseParameters"
    optional :payload, :message, 3, "grpc.testing.Payload"
    optional :response_compression, :enum, 6, "grpc.testing.CompressionType"
    optional :response_status, :message, 7, "grpc.testing.EchoStatus"
  end
  add_message "grpc.testing.StreamingOutputCallResponse" do
    optional :payload, :message, 1, "grpc.testing.Payload"
  end
  add_message "grpc.testing.ReconnectParams" do
    optional :max_reconnect_backoff_ms, :int32, 1
  end
  add_message "grpc.testing.ReconnectInfo" do
    optional :passed, :bool, 1
    repeated :backoff_ms, :int32, 2
  end
  add_enum "grpc.testing.PayloadType" do
    value :COMPRESSABLE, 0
    value :UNCOMPRESSABLE, 1
    value :RANDOM, 2
  end
  add_enum "grpc.testing.CompressionType" do
    value :NONE, 0
    value :GZIP, 1
    value :DEFLATE, 2
  end
end

module Grpc
  module Testing
    Payload = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.Payload").msgclass
    EchoStatus = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.EchoStatus").msgclass
    SimpleRequest = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.SimpleRequest").msgclass
    SimpleResponse = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.SimpleResponse").msgclass
    StreamingInputCallRequest = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.StreamingInputCallRequest").msgclass
    StreamingInputCallResponse = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.StreamingInputCallResponse").msgclass
    ResponseParameters = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ResponseParameters").msgclass
    StreamingOutputCallRequest = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.StreamingOutputCallRequest").msgclass
    StreamingOutputCallResponse = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.StreamingOutputCallResponse").msgclass
    ReconnectParams = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ReconnectParams").msgclass
    ReconnectInfo = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ReconnectInfo").msgclass
    PayloadType = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.PayloadType").enummodule
    CompressionType = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.CompressionType").enummodule
  end
end
