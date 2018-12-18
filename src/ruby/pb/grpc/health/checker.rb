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

require 'grpc'
require 'grpc/health/v1/health_services_pb'

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

      # Adds given health status for all given services
      def set_status_for_services(status, *services)
        @status_mutex.synchronize do
          services.each { |service| @statuses["#{service}"] = status }
        end
      end

      # Adds health status for each service given within hash
      def add_statuses(service_statuses = {})
        @status_mutex.synchronize do
          service_statuses.each_pair { |service, status| @statuses["#{service}"] = status }
        end
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
