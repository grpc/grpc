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
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(test_root, f)).read }
end

Server = GRPC::Core::Server

describe Server do
  def create_test_cert
    GRPC::Core::ServerCredentials.new(*load_test_certs)
  end

  before(:each) do
    @cq = GRPC::Core::CompletionQueue.new
  end

  describe '#start' do
    it 'runs without failing' do
      blk = proc { Server.new(@cq, nil).start }
      expect(&blk).to_not raise_error
    end

    it 'fails if the server is closed' do
      s = Server.new(@cq, nil)
      s.close
      expect { s.start }.to raise_error(RuntimeError)
    end
  end

  describe '#destroy' do
    it 'destroys a server ok' do
      s = start_a_server
      blk = proc { s.destroy }
      expect(&blk).to_not raise_error
    end

    it 'can be called more than once without error' do
      s = start_a_server
      begin
        blk = proc { s.destroy }
        expect(&blk).to_not raise_error
        blk.call
        expect(&blk).to_not raise_error
      ensure
        s.close
      end
    end
  end

  describe '#close' do
    it 'closes a server ok' do
      s = start_a_server
      begin
        blk = proc { s.close }
        expect(&blk).to_not raise_error
      ensure
        s.close
      end
    end

    it 'can be called more than once without error' do
      s = start_a_server
      blk = proc { s.close }
      expect(&blk).to_not raise_error
      blk.call
      expect(&blk).to_not raise_error
    end
  end

  describe '#add_http_port' do
    describe 'for insecure servers' do
      it 'runs without failing' do
        blk = proc do
          s = Server.new(@cq, nil)
          s.add_http2_port('localhost:0')
          s.close
        end
        expect(&blk).to_not raise_error
      end

      it 'fails if the server is closed' do
        s = Server.new(@cq, nil)
        s.close
        expect { s.add_http2_port('localhost:0') }.to raise_error(RuntimeError)
      end
    end

    describe 'for secure servers' do
      let(:cert) { create_test_cert }
      it 'runs without failing' do
        blk = proc do
          s = Server.new(@cq, nil)
          s.add_http2_port('localhost:0', cert)
          s.close
        end
        expect(&blk).to_not raise_error
      end

      it 'fails if the server is closed' do
        s = Server.new(@cq, nil)
        s.close
        blk = proc { s.add_http2_port('localhost:0', cert) }
        expect(&blk).to raise_error(RuntimeError)
      end
    end
  end

  shared_examples '#new' do
    it 'takes a completion queue with nil channel args' do
      expect { Server.new(@cq, nil) }.to_not raise_error
    end

    it 'does not take a hash with bad keys as channel args' do
      blk = construct_with_args(Object.new => 1)
      expect(&blk).to raise_error TypeError
      blk = construct_with_args(1 => 1)
      expect(&blk).to raise_error TypeError
    end

    it 'does not take a hash with bad values as channel args' do
      blk = construct_with_args(symbol: Object.new)
      expect(&blk).to raise_error TypeError
      blk = construct_with_args('1' => Hash.new)
      expect(&blk).to raise_error TypeError
    end

    it 'can take a hash with a symbol key as channel args' do
      blk = construct_with_args(a_symbol: 1)
      expect(&blk).to_not raise_error
    end

    it 'can take a hash with a string key as channel args' do
      blk = construct_with_args('a_symbol' => 1)
      expect(&blk).to_not raise_error
    end

    it 'can take a hash with a string value as channel args' do
      blk = construct_with_args(a_symbol: '1')
      expect(&blk).to_not raise_error
    end

    it 'can take a hash with a symbol value as channel args' do
      blk = construct_with_args(a_symbol: :another_symbol)
      expect(&blk).to_not raise_error
    end

    it 'can take a hash with a numeric value as channel args' do
      blk = construct_with_args(a_symbol: 1)
      expect(&blk).to_not raise_error
    end

    it 'can take a hash with many args as channel args' do
      args = Hash[127.times.collect { |x| [x.to_s, x] }]
      blk = construct_with_args(args)
      expect(&blk).to_not raise_error
    end
  end

  describe '#new with an insecure channel' do
    def construct_with_args(a)
      proc { Server.new(@cq, a) }
    end

    it_behaves_like '#new'
  end

  def start_a_server
    s = Server.new(@cq, nil)
    s.add_http2_port('0.0.0.0:0')
    s.start
    s
  end
end
