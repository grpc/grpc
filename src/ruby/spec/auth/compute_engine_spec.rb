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
require 'faraday'
require 'grpc/auth/compute_engine'
require 'spec_helper'

describe GRPC::Auth::GCECredentials do
  MD_URI = '/computeMetadata/v1/instance/service-accounts/default/token'
  GCECredentials = GRPC::Auth::GCECredentials

  before(:example) do
    @client = GCECredentials.new
  end

  def make_auth_stubs(with_access_token: '')
    Faraday::Adapter::Test::Stubs.new do |stub|
      stub.get(MD_URI) do |env|
        headers = env[:request_headers]
        expect(headers['Metadata-Flavor']).to eq('Google')
        build_json_response(
            'access_token' => with_access_token,
            'token_type' => 'Bearer',
            'expires_in' => 3600)
      end
    end
  end

  it_behaves_like 'apply/apply! are OK'

  describe '#on_gce?' do
    it 'should be true when Metadata-Flavor is Google' do
      stubs = Faraday::Adapter::Test::Stubs.new do |stub|
        stub.get('/') do |_env|
          [200,
           { 'Metadata-Flavor' => 'Google' },
           '']
        end
      end
      c = Faraday.new do |b|
        b.adapter(:test, stubs)
      end
      expect(GCECredentials.on_gce?(connection: c)).to eq(true)
      stubs.verify_stubbed_calls
    end

    it 'should be false when Metadata-Flavor is not Google' do
      stubs = Faraday::Adapter::Test::Stubs.new do |stub|
        stub.get('/') do |_env|
          [200,
           { 'Metadata-Flavor' => 'NotGoogle' },
           '']
        end
      end
      c = Faraday.new do |b|
        b.adapter(:test, stubs)
      end
      expect(GCECredentials.on_gce?(connection: c)).to eq(false)
      stubs.verify_stubbed_calls
    end

    it 'should be false if the response is not 200' do
      stubs = Faraday::Adapter::Test::Stubs.new do |stub|
        stub.get('/') do |_env|
          [404,
           { 'Metadata-Flavor' => 'Google' },
           '']
        end
      end
      c = Faraday.new do |b|
        b.adapter(:test, stubs)
      end
      expect(GCECredentials.on_gce?(connection: c)).to eq(false)
      stubs.verify_stubbed_calls
    end
  end
end
