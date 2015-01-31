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

require 'signet/oauth_2/client'

module Signet
  # Signet::OAuth2 supports OAuth2 authentication.
  module OAuth2
    AUTH_METADATA_KEY = :Authorization
    # Signet::OAuth2::Client creates an OAuth2 client
    #
    # Here client is re-opened to add the #apply and #apply! methods which
    # update a hash map with the fetched authentication token
    #
    # Eventually, this change may be merged into signet itself, or some other
    # package that provides Google-specific auth via signet, and this extension
    # will be unnecessary.
    class Client
      # Updates a_hash updated with the authentication token
      def apply!(a_hash, opts = {})
        # fetch the access token there is currently not one, or if the client
        # has expired
        fetch_access_token!(opts) if access_token.nil? || expired?
        a_hash[AUTH_METADATA_KEY] = "Bearer #{access_token}"
      end

      # Returns a clone of a_hash updated with the authentication token
      def apply(a_hash, opts = {})
        a_copy = a_hash.clone
        apply!(a_copy, opts)
        a_copy
      end
    end
  end
end
