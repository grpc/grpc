# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

require_relative './grpc'

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
    attr_reader :code, :details, :metadata

    include GRPC::Core::StatusCodes

    # @param code [Numeric] the status code
    # @param details [String] the details of the exception
    # @param metadata [Hash] the error's metadata
    def initialize(code, details = 'unknown cause', metadata = {})
      super("#{code}:#{details}")
      @code = code
      @details = details
      @metadata = metadata
    end

    # Converts the exception to a GRPC::Status for use in the networking
    # wrapper layer.
    #
    # @return [Status] with the same code and details
    def to_status
      Struct::Status.new(code, details, @metadata)
    end

    def self.new_status_exception(code, details = 'unkown cause', metadata = {})
      codes = {}
      codes[OK] = Ok
      codes[CANCELLED] = Cancelled
      codes[UNKNOWN] = Unknown
      codes[INVALID_ARGUMENT] = InvalidArgument
      codes[DEADLINE_EXCEEDED] = DeadlineExceeded
      codes[NOT_FOUND] = NotFound
      codes[ALREADY_EXISTS] = AlreadyExists
      codes[PERMISSION_DENIED] =  PermissionDenied
      codes[UNAUTHENTICATED] = Unauthenticated
      codes[RESOURCE_EXHAUSTED] = ResourceExhausted
      codes[FAILED_PRECONDITION] = FailedPrecondition
      codes[ABORTED] = Aborted
      codes[OUT_OF_RANGE] = OutOfRange
      codes[UNIMPLEMENTED] =  Unimplemented
      codes[INTERNAL] = Internal
      codes[UNIMPLEMENTED] =  Unimplemented
      codes[UNAVAILABLE] =  Unavailable
      codes[DATA_LOSS] = DataLoss

      if codes[code].nil?
        BadStatus.new(code, details, metadata)
      else
        codes[code].new(details, metadata)
      end
    end
  end

  # GRPC status code corresponding to status OK
  class Ok < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::OK, details, metadata)
    end
  end

  # GRPC status code corresponding to status CANCELLED
  class Cancelled < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::CANCELLED, details, metadata)
    end
  end

  # GRPC status code corresponding to status UNKNOWN
  class Unknown < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::UNKNOWN, details, metadata)
    end
  end

  # GRPC status code corresponding to status INVALID_ARGUMENT
  class InvalidArgument < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::INVALID_ARGUMENT, details, metadata)
    end
  end

  # GRPC status code corresponding to status DEADLINE_EXCEEDED
  class DeadlineExceeded < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::DEADLINE_EXCEEDED, details, metadata)
    end
  end

  # GRPC status code corresponding to status NOT_FOUND
  class NotFound < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::NOT_FOUND, details, metadata)
    end
  end

  # GRPC status code corresponding to status ALREADY_EXISTS
  class AlreadyExists < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::ALREADY_EXISTS, details, metadata)
    end
  end

  # GRPC status code corresponding to status PERMISSION_DENIED
  class PermissionDenied < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::PERMISSION_DENIED, details, metadata)
    end
  end

  # GRPC status code corresponding to status UNAUTHENTICATED
  class Unauthenticated < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::UNAUTHENTICATED, details, metadata)
    end
  end

  # GRPC status code corresponding to status RESOURCE_EXHAUSTED
  class ResourceExhausted < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::RESOURCE_EXHAUSTED, details, metadata)
    end
  end

  # GRPC status code corresponding to status FAILED_PRECONDITION
  class FailedPrecondition < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::FAILED_PRECONDITION, details, metadata)
    end
  end

  # GRPC status code corresponding to status ABORTED
  class Aborted < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::ABORTED, details, metadata)
    end
  end

  # GRPC status code corresponding to status OUT_OF_RANGE
  class OutOfRange < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::OUT_OF_RANGE, details, metadata)
    end
  end

  # GRPC status code corresponding to status UNIMPLEMENTED
  class Unimplemented < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::UNIMPLEMENTED, details, metadata)
    end
  end

  # GRPC status code corresponding to status INTERNAL
  class Internal < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::INTERNAL, details, metadata)
    end
  end

  # GRPC status code corresponding to status UNAVAILABLE
  class Unavailable < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::UNAVAILABLE, details, metadata)
    end
  end

  # GRPC status code corresponding to status DATA_LOSS
  class DataLoss < BadStatus
    def initialize(details = 'unknown cause', metadata = {})
      super(Core::StatusCodes::DATA_LOSS, details, metadata)
    end
  end
end
