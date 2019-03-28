# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: src/proto/grpc/testing/payloads.proto

require 'google/protobuf'

Google::Protobuf::DescriptorPool.generated_pool.build do
  add_file("src/proto/grpc/testing/payloads.proto", :syntax => :proto3) do
    add_message "grpc.testing.ByteBufferParams" do
      optional :req_size, :int32, 1
      optional :resp_size, :int32, 2
    end
    add_message "grpc.testing.SimpleProtoParams" do
      optional :req_size, :int32, 1
      optional :resp_size, :int32, 2
    end
    add_message "grpc.testing.ComplexProtoParams" do
    end
    add_message "grpc.testing.PayloadConfig" do
      oneof :payload do
        optional :bytebuf_params, :message, 1, "grpc.testing.ByteBufferParams"
        optional :simple_params, :message, 2, "grpc.testing.SimpleProtoParams"
        optional :complex_params, :message, 3, "grpc.testing.ComplexProtoParams"
      end
    end
  end
end

module Grpc
  module Testing
    ByteBufferParams = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ByteBufferParams").msgclass
    SimpleProtoParams = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.SimpleProtoParams").msgclass
    ComplexProtoParams = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ComplexProtoParams").msgclass
    PayloadConfig = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.PayloadConfig").msgclass
  end
end
