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

require 'grpc'
require 'grpc/health/v1/health_services_pb'
require 'thread'

module Grpc
  # Health contains classes and modules that support providing a health check
  # service.
  module Health
    # Checker is implementation of the schema-specified health checking service.
    class Checker < V1::Health::Service
      StatusCodes = GRPC::Core::StatusCodes
      HealthCheckResponse = V1::HealthCheckResponse

      # Initializes the statuses of participating services
      def initialize
        @statuses = {}
        @status_mutex = Mutex.new  # guards access to @statuses
      end

      # Implements the rpc IDL API method
      def check(req, _call)
        status = nil
        @status_mutex.synchronize do
          status = @statuses["#{req.service}"]
        end
	if status.nil?
          fail GRPC::BadStatus.new_status_exception(StatusCodes::NOT_FOUND)
	end
        HealthCheckResponse.new(status: status)
      end

      # Adds the health status for a given service.
      def add_status(service, status)
        @status_mutex.synchronize { @statuses["#{service}"] = status }
      end

      # Clears the status for the given service.
      def clear_status(service)
        @status_mutex.synchronize { @statuses.delete("#{service}") }
      end

      # Clears alls the statuses.
      def clear_all
        @status_mutex.synchronize { @statuses = {} }
      end
    end
  end
end
