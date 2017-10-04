# Copyright 2017 gRPC authors.
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

require_relative './grpc'
require 'google/rpc/status_pb'

# GRPC contains the General RPC module.
module GRPC
  # GoogleRpcStatusUtils provides utilities to convert between a
  # GRPC::Core::Status and a deserialized Google::Rpc::Status proto
  # Returns nil if the grpc-status-details-bin trailer could not be
  # converted to a GoogleRpcStatus due to the server not providing
  # the necessary trailers.
  # Raises an error if the server did provide the necessary trailers
  # but they fail to deseriliaze into a GoogleRpcStatus protobuf.
  class GoogleRpcStatusUtils
    def self.extract_google_rpc_status(status)
      fail ArgumentError, 'bad type' unless status.is_a? Struct::Status
      grpc_status_details_bin_trailer = 'grpc-status-details-bin'
      return nil if status.metadata[grpc_status_details_bin_trailer].nil?
      Google::Rpc::Status.decode(status.metadata[grpc_status_details_bin_trailer])
    end
  end
end
