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

require 'faraday'
require 'grpc/auth/signet'

module Google
  module RPC
    # Module Auth provides classes that provide Google-specific authentication
    # used to access Google gRPC services.
    module Auth
      # Extends Signet::OAuth2::Client so that the auth token is obtained from
      # the GCE metadata server.
      class GCECredentials < Signet::OAuth2::Client
        COMPUTE_AUTH_TOKEN_URI = 'http://metadata/computeMetadata/v1/'\
                                 'instance/service-accounts/default/token'
        COMPUTE_CHECK_URI = 'http://metadata.google.internal'

        # Detect if this appear to be a GCE instance, by checking if metadata
        # is available
        def self.on_gce?(options = {})
          c = options[:connection] || Faraday.default_connection
          resp = c.get(COMPUTE_CHECK_URI)
          return false unless resp.status == 200
          return false unless resp.headers.key?('Metadata-Flavor')
          return resp.headers['Metadata-Flavor'] == 'Google'
        rescue Faraday::ConnectionFailed
          return false
        end

        # Overrides the super class method to change how access tokens are
        # fetched.
        def fetch_access_token(options = {})
          c = options[:connection] || Faraday.default_connection
          c.headers = { 'Metadata-Flavor' => 'Google' }
          resp = c.get(COMPUTE_AUTH_TOKEN_URI)
          Signet::OAuth2.parse_credentials(resp.body,
                                           resp.headers['content-type'])
        end
      end
    end
  end
end
