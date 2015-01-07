## Generated from bin/math.proto for math
require "beefcake"
require "grpc"

module Math

  class DivArgs
    include Beefcake::Message
  end

  class DivReply
    include Beefcake::Message
  end

  class FibArgs
    include Beefcake::Message
  end

  class Num
    include Beefcake::Message
  end

  class FibReply
    include Beefcake::Message
  end

  class DivArgs
    required :dividend, :int64, 1
    required :divisor, :int64, 2
  end

  class DivReply
    required :quotient, :int64, 1
    required :remainder, :int64, 2
  end

  class FibArgs
    optional :limit, :int64, 1
  end

  class Num
    required :num, :int64, 1
  end

  class FibReply
    required :count, :int64, 1
  end

  module Math

    class Service
      include GRPC::GenericService

      self.marshal_class_method = :encode
      self.unmarshal_class_method = :decode

      rpc :Div, DivArgs, DivReply
      rpc :DivMany, stream(DivArgs), stream(DivReply)
      rpc :Fib, FibArgs, stream(Num)
      rpc :Sum, stream(Num), Num
    end
    Stub = Service.rpc_stub_class

  end
end
