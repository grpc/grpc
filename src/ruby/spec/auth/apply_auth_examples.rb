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

require 'faraday'
require 'spec_helper'

def build_json_response(payload)
  [200,
   { 'Content-Type' => 'application/json; charset=utf-8' },
   MultiJson.dump(payload)]
end

WANTED_AUTH_KEY = :Authorization

shared_examples 'apply/apply! are OK' do
  # tests that use these examples need to define
  #
  # @client which should be an auth client
  #
  # @make_auth_stubs, which should stub out the expected http behaviour of the
  # auth client
  describe '#fetch_access_token' do
    it 'should set access_token to the fetched value' do
      token = '1/abcdef1234567890'
      stubs = make_auth_stubs with_access_token: token
      c = Faraday.new do |b|
        b.adapter(:test, stubs)
      end

      @client.fetch_access_token!(connection: c)
      expect(@client.access_token).to eq(token)
      stubs.verify_stubbed_calls
    end
  end

  describe '#apply!' do
    it 'should update the target hash with fetched access token' do
      token = '1/abcdef1234567890'
      stubs = make_auth_stubs with_access_token: token
      c = Faraday.new do |b|
        b.adapter(:test, stubs)
      end

      md = { foo: 'bar' }
      @client.apply!(md, connection: c)
      want = { :foo => 'bar', WANTED_AUTH_KEY => "Bearer #{token}" }
      expect(md).to eq(want)
      stubs.verify_stubbed_calls
    end
  end

  describe 'updater_proc' do
    it 'should provide a proc that updates a hash with the access token' do
      token = '1/abcdef1234567890'
      stubs = make_auth_stubs with_access_token: token
      c = Faraday.new do |b|
        b.adapter(:test, stubs)
      end

      md = { foo: 'bar' }
      the_proc = @client.updater_proc
      got = the_proc.call(md, connection: c)
      want = { :foo => 'bar', WANTED_AUTH_KEY => "Bearer #{token}" }
      expect(got).to eq(want)
      stubs.verify_stubbed_calls
    end
  end

  describe '#apply' do
    it 'should not update the original hash with the access token' do
      token = '1/abcdef1234567890'
      stubs = make_auth_stubs with_access_token: token
      c = Faraday.new do |b|
        b.adapter(:test, stubs)
      end

      md = { foo: 'bar' }
      @client.apply(md, connection: c)
      want = { foo: 'bar' }
      expect(md).to eq(want)
      stubs.verify_stubbed_calls
    end

    it 'should add the token to the returned hash' do
      token = '1/abcdef1234567890'
      stubs = make_auth_stubs with_access_token: token
      c = Faraday.new do |b|
        b.adapter(:test, stubs)
      end

      md = { foo: 'bar' }
      got = @client.apply(md, connection: c)
      want = { :foo => 'bar', WANTED_AUTH_KEY => "Bearer #{token}" }
      expect(got).to eq(want)
      stubs.verify_stubbed_calls
    end

    it 'should not fetch a new token if the current is not expired' do
      token = '1/abcdef1234567890'
      stubs = make_auth_stubs with_access_token: token
      c = Faraday.new do |b|
        b.adapter(:test, stubs)
      end

      n = 5 # arbitrary
      n.times do |_t|
        md = { foo: 'bar' }
        got = @client.apply(md, connection: c)
        want = { :foo => 'bar', WANTED_AUTH_KEY => "Bearer #{token}" }
        expect(got).to eq(want)
      end
      stubs.verify_stubbed_calls
    end

    it 'should fetch a new token if the current one is expired' do
      token_1 = '1/abcdef1234567890'
      token_2 = '2/abcdef1234567890'

      [token_1, token_2].each do |t|
        stubs = make_auth_stubs with_access_token: t
        c = Faraday.new do |b|
          b.adapter(:test, stubs)
        end
        md = { foo: 'bar' }
        got = @client.apply(md, connection: c)
        want = { :foo => 'bar', WANTED_AUTH_KEY => "Bearer #{t}" }
        expect(got).to eq(want)
        stubs.verify_stubbed_calls
        @client.expires_at -= 3601 # default is to expire in 1hr
      end
    end
  end
end
