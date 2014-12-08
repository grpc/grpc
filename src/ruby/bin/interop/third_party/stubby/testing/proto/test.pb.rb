## Generated from third_party/stubby/testing/proto/test.proto for grpc.testing
require 'beefcake'
require 'grpc'

require 'third_party/stubby/testing/proto/messages.pb'
require 'net/proto2/proto/empty.pb'

module Grpc
  module Testing

    module TestService

      class Service
        include GRPC::GenericService

        self.marshal_class_method = :encode
        self.unmarshal_class_method = :decode

        rpc :EmptyCall, Proto2::Empty, Proto2::Empty
        rpc :UnaryCall, SimpleRequest, SimpleResponse
        rpc :StreamingOutputCall, StreamingOutputCallRequest, stream(StreamingOutputCallResponse)
        rpc :StreamingInputCall, stream(StreamingInputCallRequest), StreamingInputCallResponse
        rpc :FullDuplexCall, stream(StreamingOutputCallRequest), stream(StreamingOutputCallResponse)
        rpc :HalfDuplexCall, stream(StreamingOutputCallRequest), stream(StreamingOutputCallResponse)
      end
      Stub = Service.rpc_stub_class

    end
  end
end
