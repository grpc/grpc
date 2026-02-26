# Copyright 2025 gRPC authors.
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

# GRPC contains the General RPC module.

module GRPC
  module Core
    # Re-opening the C-backed Channel class to add our Ruby-level attribute.
    class Channel
      # An attribute to hold a pure Ruby call credentials object.
      # @return [GRPC::Core::CallCredentials, nil]
      attr_accessor :call_credentials
    end
  end
end