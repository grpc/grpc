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

spec_dir = File.expand_path(File.join(File.dirname(__FILE__)))
$LOAD_PATH.unshift(spec_dir)
$LOAD_PATH.uniq!

require 'apply_auth_examples'
require 'grpc/auth/service_account'
require 'jwt'
require 'multi_json'
require 'openssl'
require 'spec_helper'

describe Google::RPC::Auth::ServiceAccountCredentials do
  before(:example) do
    @key = OpenSSL::PKey::RSA.new(2048)
    cred_json = {
      private_key_id: 'a_private_key_id',
      private_key: @key.to_pem,
      client_email: 'app@developer.gserviceaccount.com',
      client_id: 'app.apps.googleusercontent.com',
      type: 'service_account'
    }
    cred_json_text = MultiJson.dump(cred_json)
    @client = Google::RPC::Auth::ServiceAccountCredentials.new(
        'https://www.googleapis.com/auth/userinfo.profile',
        StringIO.new(cred_json_text))
  end

  def make_auth_stubs(with_access_token: '')
    Faraday::Adapter::Test::Stubs.new do |stub|
      stub.post('/oauth2/v3/token') do |env|
        params = Addressable::URI.form_unencode(env[:body])
        _claim, _header = JWT.decode(params.assoc('assertion').last,
                                     @key.public_key)
        want = ['grant_type', 'urn:ietf:params:oauth:grant-type:jwt-bearer']
        expect(params.assoc('grant_type')).to eq(want)
        build_json_response(
          'access_token' => with_access_token,
          'token_type' => 'Bearer',
          'expires_in' => 3600
        )
      end
    end
  end

  it_behaves_like 'apply/apply! are OK'
end
