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

require 'grpc/auth/signet'
require 'multi_json'
require 'openssl'

# Reads the private key and client email fields from service account JSON key.
def read_json_key(json_key_io)
  json_key = MultiJson.load(json_key_io.read)
  fail 'missing client_email' unless json_key.key?('client_email')
  fail 'missing private_key' unless json_key.key?('private_key')
  [json_key['private_key'], json_key['client_email']]
end

module Google
  module RPC
    # Module Auth provides classes that provide Google-specific authentication
    # used to access Google gRPC services.
    module Auth
      # Authenticates requests using Google's Service Account credentials.
      # (cf https://developers.google.com/accounts/docs/OAuth2ServiceAccount)
      class ServiceAccountCredentials < Signet::OAuth2::Client
        TOKEN_CRED_URI = 'https://www.googleapis.com/oauth2/v3/token'
        AUDIENCE = TOKEN_CRED_URI

        # Initializes a ServiceAccountCredentials.
        #
        # @param scope [string|array] the scope(s) to access
        # @param json_key_io [IO] an IO from which the JSON key can be read
        def initialize(scope, json_key_io)
          private_key, client_email = read_json_key(json_key_io)
          super(token_credential_uri: TOKEN_CRED_URI,
                audience: AUDIENCE,
                scope: scope,
                issuer: client_email,
                signing_key: OpenSSL::PKey::RSA.new(private_key))
        end
      end
    end
  end
end
