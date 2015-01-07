## Generated from third_party/stubby/testing/proto/messages.proto for grpc.testing
require 'beefcake'

require 'net/proto2/bridge/proto/message_set.pb'

module Grpc
  module Testing

    module PayloadType
      COMPRESSABLE = 0
      UNCOMPRESSABLE = 1
      RANDOM = 2
    end

    class Payload
      include Beefcake::Message
    end

    class SimpleRequest
      include Beefcake::Message
    end

    class SimpleResponse
      include Beefcake::Message
    end

    class SimpleContext
      include Beefcake::Message
    end

    class StreamingInputCallRequest
      include Beefcake::Message
    end

    class StreamingInputCallResponse
      include Beefcake::Message
    end

    class ResponseParameters
      include Beefcake::Message
    end

    class StreamingOutputCallRequest
      include Beefcake::Message
    end

    class StreamingOutputCallResponse
      include Beefcake::Message
    end

    class Payload
      optional :type, PayloadType, 1
      optional :body, :bytes, 2
    end

    class SimpleRequest
      optional :response_type, PayloadType, 1
      optional :response_size, :int32, 2
      optional :payload, Payload, 3
    end

    class SimpleResponse
      optional :payload, Payload, 1
      optional :effective_gaia_user_id, :int64, 2
    end

    class SimpleContext
      optional :value, :string, 1
    end

    class StreamingInputCallRequest
      optional :payload, Payload, 1
    end

    class StreamingInputCallResponse
      optional :aggregated_payload_size, :int32, 1
    end

    class ResponseParameters
      optional :size, :int32, 1
      optional :interval_us, :int32, 2
    end

    class StreamingOutputCallRequest
      optional :response_type, PayloadType, 1
      repeated :response_parameters, ResponseParameters, 2
      optional :payload, Payload, 3
    end

    class StreamingOutputCallResponse
      optional :payload, Payload, 1
    end
  end
end
