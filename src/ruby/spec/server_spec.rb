# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

require 'grpc'

def load_test_certs
  test_root = File.join(File.dirname(__FILE__), 'testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  contents = files.map { |f| File.open(File.join(test_root, f)).read }
  [contents[0], [{ private_key: contents[1], cert_chain: contents[2] }], false]
end

Server = GRPC::Core::Server

describe Server do
  def create_test_cert
    GRPC::Core::ServerCredentials.new(*load_test_certs)
  end

  describe '#start' do
    it 'runs without failing' do
      blk = proc { Server.new(nil).start }
      expect(&blk).to_not raise_error
    end

    it 'fails if the server is closed' do
      s = Server.new(nil)
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
        s.close(@cq)
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
          s = Server.new(nil)
          s.add_http2_port('localhost:0', :this_port_is_insecure)
          s.close
        end
        expect(&blk).to_not raise_error
      end

      it 'fails if the server is closed' do
        s = Server.new(nil)
        s.close
        blk = proc do
          s.add_http2_port('localhost:0', :this_port_is_insecure)
        end
        expect(&blk).to raise_error(RuntimeError)
      end
    end

    describe 'for secure servers' do
      let(:cert) { create_test_cert }
      it 'runs without failing' do
        blk = proc do
          s = Server.new(nil)
          s.add_http2_port('localhost:0', cert)
          s.close
        end
        expect(&blk).to_not raise_error
      end

      it 'fails if the server is closed' do
        s = Server.new(nil)
        s.close
        blk = proc { s.add_http2_port('localhost:0', cert) }
        expect(&blk).to raise_error(RuntimeError)
      end
    end
  end

  shared_examples '#new' do
    it 'takes nil channel args' do
      expect { Server.new(nil) }.to_not raise_error
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
      blk = construct_with_args('1' => {})
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
      proc { Server.new(a) }
    end

    it_behaves_like '#new'
  end

  def start_a_server
    s = Server.new(nil)
    s.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
    s.start
    s
  end
end
