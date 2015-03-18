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
require 'xray/thread_dump_signal_handler'

def load_test_certs
  test_root = File.join(File.dirname(File.dirname(__FILE__)), 'testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(test_root, f)).read }
end

# A test message
class EchoMsg
  def self.marshal(_o)
    ''
  end

  def self.unmarshal(_o)
    EchoMsg.new
  end
end

# A test service with no methods.
class EmptyService
  include GRPC::GenericService
end

# A test service without an implementation.
class NoRpcImplementation
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg
end

# A test service with an implementation.
class EchoService
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg
  attr_reader :received_md

  def initialize(_default_var = 'ignored')
    @received_md = []
  end

  def an_rpc(req, call)
    logger.info('echo service received a request')
    @received_md << call.metadata unless call.metadata.nil?
    req
  end
end

EchoStub = EchoService.rpc_stub_class

# A slow test service.
class SlowService
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg

  def initialize(_default_var = 'ignored')
  end

  def an_rpc(req, _call)
    delay = 0.25
    logger.info("starting a slow #{delay} rpc")
    sleep delay
    req  # send back the req as the response
  end
end

SlowStub = SlowService.rpc_stub_class

describe GRPC::RpcServer do
  RpcServer = GRPC::RpcServer
  StatusCodes = GRPC::Core::StatusCodes

  before(:each) do
    @method = 'an_rpc_method'
    @pass = 0
    @fail = 1
    @noop = proc { |x| x }

    @server_queue = GRPC::Core::CompletionQueue.new
    server_host = '0.0.0.0:0'
    @server = GRPC::Core::Server.new(@server_queue, nil)
    server_port = @server.add_http2_port(server_host)
    @host = "localhost:#{server_port}"
    @ch = GRPC::Core::Channel.new(@host, nil)
  end

  after(:each) do
    @server.close
  end

  describe '#new' do
    it 'can be created with just some args' do
      opts = { a_channel_arg: 'an_arg' }
      blk = proc do
        RpcServer.new(**opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'can be created with a default deadline' do
      opts = { a_channel_arg: 'an_arg', deadline: 5 }
      blk = proc do
        RpcServer.new(**opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'can be created with a completion queue override' do
      opts = {
        a_channel_arg: 'an_arg',
        completion_queue_override: @server_queue
      }
      blk = proc do
        RpcServer.new(**opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'cannot be created with a bad completion queue override' do
      blk = proc do
        opts = {
          a_channel_arg: 'an_arg',
          completion_queue_override: Object.new
        }
        RpcServer.new(**opts)
      end
      expect(&blk).to raise_error
    end

    it 'cannot be created with invalid ServerCredentials' do
      blk = proc do
        opts = {
          a_channel_arg: 'an_arg',
          creds: Object.new
        }
        RpcServer.new(**opts)
      end
      expect(&blk).to raise_error
    end

    it 'can be created with a server override' do
      opts = { a_channel_arg: 'an_arg', server_override: @server }
      blk = proc do
        RpcServer.new(**opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'cannot be created with a bad server override' do
      blk = proc do
        opts = {
          a_channel_arg: 'an_arg',
          server_override: Object.new
        }
        RpcServer.new(**opts)
      end
      expect(&blk).to raise_error
    end
  end

  describe '#stopped?' do
    before(:each) do
      opts = { a_channel_arg: 'an_arg', poll_period: 1 }
      @srv = RpcServer.new(**opts)
    end

    it 'starts out false' do
      expect(@srv.stopped?).to be(false)
    end

    it 'stays false after a #stop is called before #run' do
      @srv.stop
      expect(@srv.stopped?).to be(false)
    end

    it 'stays false after the server starts running' do
      @srv.handle(EchoService)
      t = Thread.new { @srv.run }
      @srv.wait_till_running
      expect(@srv.stopped?).to be(false)
      @srv.stop
      t.join
    end

    it 'is true after a running server is stopped' do
      @srv.handle(EchoService)
      t = Thread.new { @srv.run }
      @srv.wait_till_running
      @srv.stop
      expect(@srv.stopped?).to be(true)
      t.join
    end
  end

  describe '#running?' do
    it 'starts out false' do
      opts = { a_channel_arg: 'an_arg', server_override: @server }
      r = RpcServer.new(**opts)
      expect(r.running?).to be(false)
    end

    it 'is false after run is called with no services registered' do
      opts = {
        a_channel_arg: 'an_arg',
        poll_period: 1,
        server_override: @server
      }
      r = RpcServer.new(**opts)
      r.run
      expect(r.running?).to be(false)
    end

    it 'is true after run is called with a registered service' do
      opts = {
        a_channel_arg: 'an_arg',
        poll_period: 1,
        server_override: @server
      }
      r = RpcServer.new(**opts)
      r.handle(EchoService)
      t = Thread.new { r.run }
      r.wait_till_running
      expect(r.running?).to be(true)
      r.stop
      t.join
    end
  end

  describe '#handle' do
    before(:each) do
      @opts = { a_channel_arg: 'an_arg', poll_period: 1 }
      @srv = RpcServer.new(**@opts)
    end

    it 'raises if #run has already been called' do
      @srv.handle(EchoService)
      t = Thread.new { @srv.run }
      @srv.wait_till_running
      expect { @srv.handle(EchoService) }.to raise_error
      @srv.stop
      t.join
    end

    it 'raises if the server has been run and stopped' do
      @srv.handle(EchoService)
      t = Thread.new { @srv.run }
      @srv.wait_till_running
      @srv.stop
      t.join
      expect { @srv.handle(EchoService) }.to raise_error
    end

    it 'raises if the service does not include GenericService ' do
      expect { @srv.handle(Object) }.to raise_error
    end

    it 'raises if the service does not declare any rpc methods' do
      expect { @srv.handle(EmptyService) }.to raise_error
    end

    it 'raises if the service does not define its rpc methods' do
      expect { @srv.handle(NoRpcImplementation) }.to raise_error
    end

    it 'raises if a handler method is already registered' do
      @srv.handle(EchoService)
      expect { r.handle(EchoService) }.to raise_error
    end
  end

  describe '#run' do
    before(:each) do
      @client_opts = {
        channel_override: @ch
      }
      @marshal = EchoService.rpc_descs[:an_rpc].marshal_proc
      @unmarshal = EchoService.rpc_descs[:an_rpc].unmarshal_proc(:output)
      server_opts = {
        server_override: @server,
        completion_queue_override: @server_queue,
        poll_period: 1
      }
      @srv = RpcServer.new(**server_opts)
    end

    describe 'when running' do
      it 'should return NOT_FOUND status on unknown methods', server: true do
        @srv.handle(EchoService)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        req = EchoMsg.new
        blk = proc do
          cq = GRPC::Core::CompletionQueue.new
          stub = GRPC::ClientStub.new(@host, cq, **@client_opts)
          stub.request_response('/unknown', req, @marshal, @unmarshal)
        end
        expect(&blk).to raise_error GRPC::BadStatus
        @srv.stop
        t.join
      end

      it 'should handle multiple sequential requests', server: true do
        @srv.handle(EchoService)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        req = EchoMsg.new
        n = 5  # arbitrary
        stub = EchoStub.new(@host, **@client_opts)
        n.times { expect(stub.an_rpc(req)).to be_a(EchoMsg) }
        @srv.stop
        t.join
      end

      it 'should receive metadata sent as rpc keyword args', server: true do
        service = EchoService.new
        @srv.handle(service)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        req = EchoMsg.new
        stub = EchoStub.new(@host, **@client_opts)
        expect(stub.an_rpc(req, k1: 'v1', k2: 'v2')).to be_a(EchoMsg)
        wanted_md = [{ 'k1' => 'v1', 'k2' => 'v2' }]
        expect(service.received_md).to eq(wanted_md)
        @srv.stop
        t.join
      end

      it 'should receive updated metadata', server: true do
        service = EchoService.new
        @srv.handle(service)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        req = EchoMsg.new
        @client_opts[:update_metadata] = proc do |md|
          md[:k1] = 'updated-v1'
          md
        end
        stub = EchoStub.new(@host, **@client_opts)
        expect(stub.an_rpc(req, k1: 'v1', k2: 'v2')).to be_a(EchoMsg)
        wanted_md = [{ 'k1' => 'updated-v1', 'k2' => 'v2' }]
        expect(service.received_md).to eq(wanted_md)
        @srv.stop
        t.join
      end

      it 'should handle multiple parallel requests', server: true do
        @srv.handle(EchoService)
        Thread.new { @srv.run }
        @srv.wait_till_running
        req, q = EchoMsg.new, Queue.new
        n = 5  # arbitrary
        threads = []
        n.times do
          threads << Thread.new do
            stub = EchoStub.new(@host, **@client_opts)
            q << stub.an_rpc(req)
          end
        end
        n.times { expect(q.pop).to be_a(EchoMsg) }
        @srv.stop
        threads.each(&:join)
      end

      it 'should return UNAVAILABLE on too many jobs', server: true do
        opts = {
          a_channel_arg: 'an_arg',
          server_override: @server,
          completion_queue_override: @server_queue,
          pool_size: 1,
          poll_period: 1,
          max_waiting_requests: 0
        }
        alt_srv = RpcServer.new(**opts)
        alt_srv.handle(SlowService)
        Thread.new { alt_srv.run }
        alt_srv.wait_till_running
        req = EchoMsg.new
        n = 5  # arbitrary, use as many to ensure the server pool is exceeded
        threads = []
        one_failed_as_unavailable = false
        n.times do
          threads << Thread.new do
            stub = SlowStub.new(@host, **@client_opts)
            begin
              stub.an_rpc(req)
            rescue GRPC::BadStatus => e
              one_failed_as_unavailable = e.code == StatusCodes::UNAVAILABLE
            end
          end
        end
        threads.each(&:join)
        alt_srv.stop
        expect(one_failed_as_unavailable).to be(true)
      end
    end
  end
end
