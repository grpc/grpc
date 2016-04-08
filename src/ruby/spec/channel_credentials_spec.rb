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

require 'grpc'

describe GRPC::Core::ChannelCredentials do
  ChannelCredentials = GRPC::Core::ChannelCredentials
  CallCredentials = GRPC::Core::CallCredentials

  def load_test_certs
    test_root = File.join(File.dirname(__FILE__), 'testdata')
    files = ['ca.pem', 'server1.pem', 'server1.key']
    files.map { |f| File.open(File.join(test_root, f)).read }
  end

  describe '#new' do
    it 'can be constructed with fake inputs' do
      blk = proc  { ChannelCredentials.new('root_certs', 'key', 'cert') }
      expect(&blk).not_to raise_error
    end

    it 'it can be constructed using specific test certificates' do
      certs = load_test_certs
      expect { ChannelCredentials.new(*certs) }.not_to raise_error
    end

    it 'can be constructed with server roots certs only' do
      root_cert, _, _ = load_test_certs
      expect { ChannelCredentials.new(root_cert) }.not_to raise_error
    end

    it 'can be constructed with a nil server roots' do
      _, client_key, client_chain = load_test_certs
      blk = proc { ChannelCredentials.new(nil, client_key, client_chain) }
      expect(&blk).not_to raise_error
    end

    it 'can be constructed with no params' do
      blk = proc { ChannelCredentials.new(nil) }
      expect(&blk).not_to raise_error
    end
  end

  describe '#compose' do
    it 'can compose with a CallCredentials' do
      certs = load_test_certs
      channel_creds = ChannelCredentials.new(*certs)
      auth_proc = proc { { 'plugin_key' => 'plugin_value' } }
      call_creds = CallCredentials.new auth_proc
      expect { channel_creds.compose call_creds }.not_to raise_error
    end

    it 'can compose with multiple CallCredentials' do
      certs = load_test_certs
      channel_creds = ChannelCredentials.new(*certs)
      auth_proc = proc { { 'plugin_key' => 'plugin_value' } }
      call_creds1 = CallCredentials.new auth_proc
      call_creds2 = CallCredentials.new auth_proc
      expect do
        channel_creds.compose(call_creds1, call_creds2)
      end.not_to raise_error
    end

    it 'cannot compose with ChannelCredentials' do
      certs = load_test_certs
      channel_creds1 = ChannelCredentials.new(*certs)
      channel_creds2 = ChannelCredentials.new(*certs)
      expect { channel_creds1.compose channel_creds2 }.to raise_error(TypeError)
    end
  end
end
