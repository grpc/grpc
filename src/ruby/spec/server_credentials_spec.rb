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

def load_test_certs
  test_root = File.join(File.dirname(__FILE__), 'testdata')
  files = ['ca.pem', 'server1.pem', 'server1.key']
  files.map { |f| File.open(File.join(test_root, f)).read }
end

describe GRPC::Core::ServerCredentials do
  Creds = GRPC::Core::ServerCredentials

  describe '#new' do
    it 'can be constructed from a fake CA PEM, server PEM and a server key' do
      expect { Creds.new('a', 'b', 'c') }.not_to raise_error
    end

    it 'can be constructed using the test certificates' do
      certs = load_test_certs
      expect { Creds.new(*certs) }.not_to raise_error
    end

    it 'cannot be constructed without a server cert chain' do
      root_cert, server_key, _ = load_test_certs
      blk = proc { Creds.new(root_cert, server_key, nil) }
      expect(&blk).to raise_error
    end

    it 'cannot be constructed without a server key' do
      root_cert, _, _ = load_test_certs
      blk = proc { Creds.new(root_cert, nil, cert_chain) }
      expect(&blk).to raise_error
    end

    it 'can be constructed without a root_cret' do
      _, server_key, cert_chain = load_test_certs
      blk = proc { Creds.new(nil, server_key, cert_chain) }
      expect(&blk).to_not raise_error
    end
  end
end
