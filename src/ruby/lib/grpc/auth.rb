# Copyright 2014, Google Inc.
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
require 'signet'


module Google
  import Signet::OAuth2

  # Google::RPC contains the General RPC module.
  module RPC
    # ServiceAccounCredentials can obtain credentials for a configured service
    # account, scopes and issuer.
    module Auth
      class ServiceAccountCredentials
        CREDENTIAL_URI = 'https://accounts.google.com/o/oauth2/token'
        AUDIENCE_URI = 'https://accounts.google.com/o/oauth2/token'

        # Initializes an instance with the given scope, issuer and signing_key
        def initialize(scope, issuer, key)
          @auth_client =  Client.new(token_credential_uri: CREDENTIAL_URI,
                                     audience: AUDIENCE_URI,
                                     scope: scope,
                                     issuer: issuer,
                                     signing_key: key)
          @auth_token = nil
        end

        def metadata_update_proc
          proc do |input_md|
            input
          end
        end

        def auth_creds
          key = Google::APIClient::KeyUtils.load_from_pkcs12('client.p12', 'notasecret')
        end
      end
    end
  end
end
