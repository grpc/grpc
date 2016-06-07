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
  class BadStatus < StandardError
    attr_reader :code, :details, :metadata

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
  end

  # Cancelled is an exception class that indicates that an rpc was cancelled.
  class Cancelled < StandardError
  end
end
