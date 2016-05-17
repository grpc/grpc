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
require 'grpc/generic/rpc_desc'
require 'grpc/generic/service'

# A test message that encodes/decodes using marshal/marshal.
class GoodMsg
  def self.marshal(_o)
    ''
  end

  def self.unmarshal(_o)
    GoodMsg.new
  end
end

# A test message that encodes/decodes using encode/decode.
class EncodeDecodeMsg
  def self.encode(_o)
    ''
  end

  def self.decode(_o)
    GoodMsg.new
  end
end

GenericService = GRPC::GenericService
Dsl = GenericService::Dsl

describe Dsl do
  it 'can be included in new classes' do
    blk = proc { Class.new { include Dsl } }
    expect(&blk).to_not raise_error
  end
end

describe GenericService do
  context '#underscore' do
    it 'should convert CamelCase to underscore separated' do
      expect(GenericService.underscore('AnRPC')).to eq('an_rpc')
      expect(GenericService.underscore('AMethod')).to eq('a_method')
      expect(GenericService.underscore('PrintHTML')).to eq('print_html')
      expect(GenericService.underscore('SeeHTMLBooks')).to eq('see_html_books')
    end
  end

  describe 'including it' do
    it 'adds a class method, rpc' do
      c = Class.new do
        include GenericService
      end
      expect(c.methods).to include(:rpc)
    end

    it 'adds rpc descs using the added class method, #rpc' do
      c = Class.new do
        include GenericService
        rpc :AnRpc, GoodMsg, GoodMsg
      end

      expect(c.rpc_descs).to include(:AnRpc)
      expect(c.rpc_descs[:AnRpc]).to be_a(GRPC::RpcDesc)
    end

    it 'give subclasses access to #rpc_descs' do
      base = Class.new do
        include GenericService
        rpc :AnRpc, GoodMsg, GoodMsg
      end
      c = Class.new(base) do
      end
      expect(c.rpc_descs).to include(:AnRpc)
      expect(c.rpc_descs[:AnRpc]).to be_a(GRPC::RpcDesc)
    end

    it 'adds a default service name' do
      c = Class.new do
        include GenericService
      end
      expect(c.service_name).to eq('GenericService')
    end

    it 'adds a default service name to subclasses' do
      base = Class.new do
        include GenericService
      end
      c = Class.new(base) do
      end
      expect(c.service_name).to eq('GenericService')
    end

    it 'adds the specified service name' do
      c = Class.new do
        include GenericService
        self.service_name = 'test.service.TestService'
      end
      expect(c.service_name).to eq('test.service.TestService')
    end

    it 'adds the specified service name to subclasses' do
      base = Class.new do
        include GenericService
        self.service_name = 'test.service.TestService'
      end
      c = Class.new(base) do
      end
      expect(c.service_name).to eq('test.service.TestService')
    end
  end

  describe '#include' do
    it 'raises if #rpc is missing an arg' do
      blk = proc do
        Class.new do
          include GenericService
          rpc :AnRpc, GoodMsg
        end
      end
      expect(&blk).to raise_error ArgumentError

      blk = proc do
        Class.new do
          include GenericService
          rpc :AnRpc
        end
      end
      expect(&blk).to raise_error ArgumentError
    end

    describe 'when #rpc args are incorrect' do
      it 'raises if an arg does not have the marshal or unmarshal methods' do
        blk = proc do
          Class.new do
            include GenericService
            rpc :AnRpc, GoodMsg, Object
          end
        end
        expect(&blk).to raise_error ArgumentError
      end

      it 'raises if a type arg only has the marshal method' do
        # a bad message type with only a marshal method
        class OnlyMarshal
          def marshal(o)
            o
          end
        end

        blk = proc do
          Class.new do
            include GenericService
            rpc :AnRpc, OnlyMarshal, GoodMsg
          end
        end
        expect(&blk).to raise_error ArgumentError
      end

      it 'raises if a type arg only has the unmarshal method' do
        # a bad message type with only an unmarshal method
        class OnlyUnmarshal
          def self.ummarshal(o)
            o
          end
        end
        blk = proc do
          Class.new do
            include GenericService
            rpc :AnRpc, GoodMsg, OnlyUnmarshal
          end
        end
        expect(&blk).to raise_error ArgumentError
      end
    end

    it 'is ok for services that expect the default {un,}marshal methods' do
      blk = proc do
        Class.new do
          include GenericService
          rpc :AnRpc, GoodMsg, GoodMsg
        end
      end
      expect(&blk).not_to raise_error
    end

    it 'is ok for services that override the default {un,}marshal methods' do
      blk = proc do
        Class.new do
          include GenericService
          self.marshal_class_method = :encode
          self.unmarshal_class_method = :decode
          rpc :AnRpc, EncodeDecodeMsg, EncodeDecodeMsg
        end
      end
      expect(&blk).not_to raise_error
    end
  end

  describe '#rpc_stub_class' do
    it 'generates a client class that defines any of the rpc methods' do
      s = Class.new do
        include GenericService
        rpc :AnRpc, GoodMsg, GoodMsg
        rpc :AServerStreamer, GoodMsg, stream(GoodMsg)
        rpc :AClientStreamer, stream(GoodMsg), GoodMsg
        rpc :ABidiStreamer, stream(GoodMsg), stream(GoodMsg)
      end
      client_class = s.rpc_stub_class
      expect(client_class.instance_methods).to include(:an_rpc)
      expect(client_class.instance_methods).to include(:a_server_streamer)
      expect(client_class.instance_methods).to include(:a_client_streamer)
      expect(client_class.instance_methods).to include(:a_bidi_streamer)
    end

    describe 'the generated instances' do
      it 'can be instanciated with just a hostname and credentials' do
        s = Class.new do
          include GenericService
          rpc :AnRpc, GoodMsg, GoodMsg
          rpc :AServerStreamer, GoodMsg, stream(GoodMsg)
          rpc :AClientStreamer, stream(GoodMsg), GoodMsg
          rpc :ABidiStreamer, stream(GoodMsg), stream(GoodMsg)
        end
        client_class = s.rpc_stub_class
        blk = proc do
          client_class.new('fakehostname', :this_channel_is_insecure)
        end
        expect(&blk).not_to raise_error
      end

      it 'has the methods defined in the service' do
        s = Class.new do
          include GenericService
          rpc :AnRpc, GoodMsg, GoodMsg
          rpc :AServerStreamer, GoodMsg, stream(GoodMsg)
          rpc :AClientStreamer, stream(GoodMsg), GoodMsg
          rpc :ABidiStreamer, stream(GoodMsg), stream(GoodMsg)
        end
        client_class = s.rpc_stub_class
        o = client_class.new('fakehostname', :this_channel_is_insecure)
        expect(o.methods).to include(:an_rpc)
        expect(o.methods).to include(:a_bidi_streamer)
        expect(o.methods).to include(:a_client_streamer)
        expect(o.methods).to include(:a_bidi_streamer)
      end
    end
  end
end
