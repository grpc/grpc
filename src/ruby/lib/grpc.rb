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

ssl_roots_path = File.expand_path('../../../../etc/roots.pem', __FILE__)

require_relative 'grpc/errors'
require_relative 'grpc/grpc'
require_relative 'grpc/logconfig'
require_relative 'grpc/notifier'
require_relative 'grpc/version'
require_relative 'grpc/core/time_consts'
require_relative 'grpc/generic/active_call'
require_relative 'grpc/generic/client_stub'
require_relative 'grpc/generic/service'
require_relative 'grpc/generic/rpc_server'
require_relative 'grpc/generic/interceptors'

begin
  file = File.open(ssl_roots_path)
  roots = file.read
  GRPC::Core::ChannelCredentials.set_default_roots_pem roots
ensure
  file.close
end
