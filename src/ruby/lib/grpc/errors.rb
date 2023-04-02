# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

require_relative './structs'
require_relative './core/status_codes'
require_relative './google_rpc_status_utils'

# GRPC contains the General RPC module.
module GRPC
  # BadStatus is an exception class that indicates that an error occurred at
  # either end of a GRPC connection.  When raised, it indicates that a status
  # error should be returned to the other end of a GRPC connection; when
  # caught it means that this end received a status error.
  #
  # There is also subclass of BadStatus in this module for each GRPC status.
  # E.g., the GRPC::Cancelled class corresponds to status CANCELLED.
  #
  # See
  # https://github.com/grpc/grpc/blob/master/include/grpc/impl/codegen/status.h
  # for detailed descriptions of each status code.
  class BadStatus < StandardError
    attr_reader :code, :details, :metadata, :debug_error_string

    include GRPC::Core::StatusCodes

    # @param code [Numeric] the status code
    # @param details [String] the details of the exception
    # @param metadata [Hash] the error's metadata
    def initialize(code,
                   details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      exception_message = "#{code}:#{details}"
      if debug_error_string
        exception_message += ". debug_error_string:{#{debug_error_string}}"
      end
      super(exception_message)
      @code = code
      @details = details
      @metadata = metadata
      @debug_error_string = debug_error_string
    end

    # Converts the exception to a {Struct::Status} for use in the networking
    # wrapper layer.
    #
    # @return [Struct::Status] with the same code and details
    def to_status
      Struct::Status.new(code, details, metadata, debug_error_string)
    end

    # Converts the exception to a deserialized {Google::Rpc::Status} object.
    # Returns `nil` if the `grpc-status-details-bin` trailer could not be
    # converted to a {Google::Rpc::Status} due to the server not providing
    # the necessary trailers.
    #
    # @return [Google::Rpc::Status, nil]
    def to_rpc_status
      GoogleRpcStatusUtils.extract_google_rpc_status(to_status)
    rescue Google::Protobuf::ParseError => parse_error
      GRPC.logger.warn('parse error: to_rpc_status failed')
      GRPC.logger.warn(parse_error)
      nil
    end

    def self.new_status_exception(code,
                                  details = 'unknown cause',
                                  metadata = {},
                                  debug_error_string = nil)
      codes = {}
      codes[OK] = Ok
      codes[CANCELLED] = Cancelled
      codes[UNKNOWN] = Unknown
      codes[INVALID_ARGUMENT] = InvalidArgument
      codes[DEADLINE_EXCEEDED] = DeadlineExceeded
      codes[NOT_FOUND] = NotFound
      codes[ALREADY_EXISTS] = AlreadyExists
      codes[PERMISSION_DENIED] = PermissionDenied
      codes[UNAUTHENTICATED] = Unauthenticated
      codes[RESOURCE_EXHAUSTED] = ResourceExhausted
      codes[FAILED_PRECONDITION] = FailedPrecondition
      codes[ABORTED] = Aborted
      codes[OUT_OF_RANGE] = OutOfRange
      codes[UNIMPLEMENTED] = Unimplemented
      codes[INTERNAL] = Internal
      codes[UNAVAILABLE] = Unavailable
      codes[DATA_LOSS] = DataLoss

      if codes[code].nil?
        BadStatus.new(code, details, metadata, debug_error_string)
      else
        codes[code].new(details, metadata, debug_error_string)
      end
    end
  end

  # GRPC status code corresponding to status OK
  class Ok < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::OK,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status CANCELLED
  class Cancelled < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::CANCELLED,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status UNKNOWN
  class Unknown < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::UNKNOWN,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status INVALID_ARGUMENT
  class InvalidArgument < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::INVALID_ARGUMENT,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status DEADLINE_EXCEEDED
  class DeadlineExceeded < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::DEADLINE_EXCEEDED,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status NOT_FOUND
  class NotFound < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::NOT_FOUND,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status ALREADY_EXISTS
  class AlreadyExists < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::ALREADY_EXISTS,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status PERMISSION_DENIED
  class PermissionDenied < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::PERMISSION_DENIED,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status UNAUTHENTICATED
  class Unauthenticated < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::UNAUTHENTICATED,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status RESOURCE_EXHAUSTED
  class ResourceExhausted < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::RESOURCE_EXHAUSTED,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status FAILED_PRECONDITION
  class FailedPrecondition < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::FAILED_PRECONDITION,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status ABORTED
  class Aborted < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::ABORTED,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status OUT_OF_RANGE
  class OutOfRange < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::OUT_OF_RANGE,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status UNIMPLEMENTED
  class Unimplemented < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::UNIMPLEMENTED,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status INTERNAL
  class Internal < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::INTERNAL,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status UNAVAILABLE
  class Unavailable < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::UNAVAILABLE,
            details, metadata, debug_error_string)
    end
  end

  # GRPC status code corresponding to status DATA_LOSS
  class DataLoss < BadStatus
    def initialize(details = 'unknown cause',
                   metadata = {},
                   debug_error_string = nil)
      super(Core::StatusCodes::DATA_LOSS,
            details, metadata, debug_error_string)
    end
  end
end
