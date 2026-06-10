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
require_relative 'grpc/structs'
require_relative 'grpc/grpc'
require_relative 'grpc/logconfig'
require_relative 'grpc/notifier'
require_relative 'grpc/version'
require_relative 'grpc/core/status_codes'
require_relative 'grpc/core/time_consts'

# Feature toggle: set GRPC_EXPERIMENTS=pure_ruby_call_credentials to enable.
# Default (false): uses C extension path for backward compatibility.
# NOTE: must be set before `require 'grpc'` — evaluated once at load time.
module GRPC
  PURE_RUBY_CALL_CREDENTIALS_ENABLED =
    ENV.fetch('GRPC_EXPERIMENTS', '')
       .split(',')
       .map(&:strip)
       .include?('pure_ruby_call_credentials')
       .freeze
end

if GRPC::PURE_RUBY_CALL_CREDENTIALS_ENABLED
  require_relative 'grpc/core/call_credentials'
  require_relative 'grpc/core/channel_credentials'
  require_relative 'grpc/core/credentials_helper'
end

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

# Prepend CompositeCredentialsHandler if pure Ruby credentials are enabled.
# This must happen after all credential classes are loaded.
if GRPC::PURE_RUBY_CALL_CREDENTIALS_ENABLED
  GRPC::ClientStub.class_eval { prepend GRPC::Core::CompositeCredentialsHandler }
end
