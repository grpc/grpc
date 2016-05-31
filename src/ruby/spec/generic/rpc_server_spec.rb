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
  test_root = File.join(File.dirname(File.dirname(__FILE__)), 'testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(test_root, f)).read }
end

def check_md(wanted_md, received_md)
  wanted_md.zip(received_md).each do |w, r|
    w.each do |key, value|
      expect(r[key]).to eq(value)
    end
  end
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

# A test service with an echo implementation.
class EchoService
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg
  attr_reader :received_md

  def initialize(**kw)
    @trailing_metadata = kw
    @received_md = []
  end

  def an_rpc(req, call)
    GRPC.logger.info('echo service received a request')
    call.output_metadata.update(@trailing_metadata)
    @received_md << call.metadata unless call.metadata.nil?
    req
  end
end

EchoStub = EchoService.rpc_stub_class

# A test service with an implementation that fails with BadStatus
class FailingService
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg
  attr_reader :details, :code, :md

  def initialize(_default_var = 'ignored')
    @details = 'app error'
    @code = 101
    @md = { failed_method: 'an_rpc' }
  end

  def an_rpc(_req, _call)
    fail GRPC::BadStatus.new(@code, @details, @md)
  end
end

FailingStub = FailingService.rpc_stub_class

# A slow test service.
class SlowService
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg
  attr_reader :received_md, :delay

  def initialize(_default_var = 'ignored')
    @delay = 0.25
    @received_md = []
  end

  def an_rpc(req, call)
    GRPC.logger.info("starting a slow #{@delay} rpc")
    sleep @delay
    @received_md << call.metadata unless call.metadata.nil?
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
  end

  describe '#new' do
    it 'can be created with just some args' do
      opts = { server_args: { a_channel_arg: 'an_arg' } }
      blk = proc do
        RpcServer.new(**opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'can be created with a completion queue override' do
      opts = {
        server_args: { a_channel_arg: 'an_arg' },
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
          server_args: { a_channel_arg: 'an_arg' },
          completion_queue_override: Object.new
        }
        RpcServer.new(**opts)
      end
      expect(&blk).to raise_error
    end

    it 'cannot be created with invalid ServerCredentials' do
      blk = proc do
        opts = {
          server_args: { a_channel_arg: 'an_arg' },
          creds: Object.new
        }
        RpcServer.new(**opts)
      end
      expect(&blk).to raise_error
    end
  end

  describe '#stopped?' do
    before(:each) do
      opts = { server_args: { a_channel_arg: 'an_arg' }, poll_period: 1.5 }
      @srv = RpcServer.new(**opts)
      @srv.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
    end

    it 'starts out false' do
      expect(@srv.stopped?).to be(false)
    end

    it 'stays false after the server starts running', server: true do
      @srv.handle(EchoService)
      t = Thread.new { @srv.run }
      @srv.wait_till_running
      expect(@srv.stopped?).to be(false)
      @srv.stop
      t.join
    end

    it 'is true after a running server is stopped', server: true do
      @srv.handle(EchoService)
      t = Thread.new { @srv.run }
      @srv.wait_till_running
      @srv.stop
      t.join
      expect(@srv.stopped?).to be(true)
    end
  end

  describe '#running?' do
    it 'starts out false' do
      opts = {
        server_args: { a_channel_arg: 'an_arg' }
      }
      r = RpcServer.new(**opts)
      expect(r.running?).to be(false)
    end

    it 'is false if run is called with no services registered', server: true do
      opts = {
        server_args: { a_channel_arg: 'an_arg' },
        poll_period: 2
      }
      r = RpcServer.new(**opts)
      r.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
      expect { r.run }.to raise_error(RuntimeError)
    end

    it 'is true after run is called with a registered service' do
      opts = {
        server_args: { a_channel_arg: 'an_arg' },
        poll_period: 2.5
      }
      r = RpcServer.new(**opts)
      r.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
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
      @opts = { server_args: { a_channel_arg: 'an_arg' }, poll_period: 1 }
      @srv = RpcServer.new(**@opts)
      @srv.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
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

    it 'raises if a handler method is already registered' do
      @srv.handle(EchoService)
      expect { r.handle(EchoService) }.to raise_error
    end
  end

  describe '#run' do
    let(:client_opts) { { channel_override: @ch } }
    let(:marshal) { EchoService.rpc_descs[:an_rpc].marshal_proc }
    let(:unmarshal) { EchoService.rpc_descs[:an_rpc].unmarshal_proc(:output) }

    context 'with no connect_metadata' do
      before(:each) do
        server_opts = {
          completion_queue_override: @server_queue,
          poll_period: 1
        }
        @srv = RpcServer.new(**server_opts)
        server_port = @srv.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
        @host = "localhost:#{server_port}"
        @ch = GRPC::Core::Channel.new(@host, nil, :this_channel_is_insecure)
      end

      it 'should return NOT_FOUND status on unknown methods', server: true do
        @srv.handle(EchoService)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        req = EchoMsg.new
        blk = proc do
          cq = GRPC::Core::CompletionQueue.new
          stub = GRPC::ClientStub.new(@host, cq, :this_channel_is_insecure,
                                      **client_opts)
          stub.request_response('/unknown', req, marshal, unmarshal)
        end
        expect(&blk).to raise_error GRPC::BadStatus
        @srv.stop
        t.join
      end

      it 'should return UNIMPLEMENTED on unimplemented methods', server: true do
        @srv.handle(NoRpcImplementation)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        req = EchoMsg.new
        blk = proc do
          cq = GRPC::Core::CompletionQueue.new
          stub = GRPC::ClientStub.new(@host, cq, :this_channel_is_insecure,
                                      **client_opts)
          stub.request_response('/an_rpc', req, marshal, unmarshal)
        end
        expect(&blk).to raise_error do |error|
          expect(error).to be_a(GRPC::BadStatus)
          expect(error.code).to be(GRPC::Core::StatusCodes::UNIMPLEMENTED)
        end
        @srv.stop
        t.join
      end

      it 'should handle multiple sequential requests', server: true do
        @srv.handle(EchoService)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        req = EchoMsg.new
        n = 5  # arbitrary
        stub = EchoStub.new(@host, :this_channel_is_insecure, **client_opts)
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
        stub = EchoStub.new(@host, :this_channel_is_insecure, **client_opts)
        expect(stub.an_rpc(req, metadata: { k1: 'v1', k2: 'v2' }))
          .to be_a(EchoMsg)
        wanted_md = [{ 'k1' => 'v1', 'k2' => 'v2' }]
        check_md(wanted_md, service.received_md)
        @srv.stop
        t.join
      end

      it 'should receive metadata if a deadline is specified', server: true do
        service = SlowService.new
        @srv.handle(service)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        req = EchoMsg.new
        stub = SlowStub.new(@host, :this_channel_is_insecure, **client_opts)
        timeout = service.delay + 1.0
        deadline = GRPC::Core::TimeConsts.from_relative_time(timeout)
        resp = stub.an_rpc(req,
                           deadline: deadline,
                           metadata: { k1: 'v1', k2: 'v2' })
        expect(resp).to be_a(EchoMsg)
        wanted_md = [{ 'k1' => 'v1', 'k2' => 'v2' }]
        check_md(wanted_md, service.received_md)
        @srv.stop
        t.join
      end

      it 'should handle cancellation correctly', server: true do
        service = SlowService.new
        @srv.handle(service)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        req = EchoMsg.new
        stub = SlowStub.new(@host, :this_channel_is_insecure, **client_opts)
        op = stub.an_rpc(req, metadata: { k1: 'v1', k2: 'v2' }, return_op: true)
        Thread.new do  # cancel the call
          sleep 0.1
          op.cancel
        end
        expect { op.execute }.to raise_error GRPC::Cancelled
        @srv.stop
        t.join
      end

      it 'should handle multiple parallel requests', server: true do
        @srv.handle(EchoService)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        req, q = EchoMsg.new, Queue.new
        n = 5  # arbitrary
        threads = [t]
        n.times do
          threads << Thread.new do
            stub = EchoStub.new(@host, :this_channel_is_insecure, **client_opts)
            q << stub.an_rpc(req)
          end
        end
        n.times { expect(q.pop).to be_a(EchoMsg) }
        @srv.stop
        threads.each(&:join)
      end

      it 'should return RESOURCE_EXHAUSTED on too many jobs', server: true do
        opts = {
          server_args: { a_channel_arg: 'an_arg' },
          completion_queue_override: @server_queue,
          pool_size: 1,
          poll_period: 1,
          max_waiting_requests: 0
        }
        alt_srv = RpcServer.new(**opts)
        alt_srv.handle(SlowService)
        alt_port = alt_srv.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
        alt_host = "0.0.0.0:#{alt_port}"
        t = Thread.new { alt_srv.run }
        alt_srv.wait_till_running
        req = EchoMsg.new
        n = 5  # arbitrary, use as many to ensure the server pool is exceeded
        threads = []
        one_failed_as_unavailable = false
        n.times do
          threads << Thread.new do
            stub = SlowStub.new(alt_host, :this_channel_is_insecure)
            begin
              stub.an_rpc(req)
            rescue GRPC::BadStatus => e
              one_failed_as_unavailable =
                e.code == StatusCodes::RESOURCE_EXHAUSTED
            end
          end
        end
        threads.each(&:join)
        alt_srv.stop
        t.join
        expect(one_failed_as_unavailable).to be(true)
      end
    end

    context 'with connect metadata' do
      let(:test_md_proc) do
        proc do |mth, md|
          res = md.clone
          res['method'] = mth
          res['connect_k1'] = 'connect_v1'
          res
        end
      end
      before(:each) do
        server_opts = {
          completion_queue_override: @server_queue,
          poll_period: 1,
          connect_md_proc: test_md_proc
        }
        @srv = RpcServer.new(**server_opts)
        alt_port = @srv.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
        @alt_host = "0.0.0.0:#{alt_port}"
      end

      it 'should send connect metadata to the client', server: true do
        service = EchoService.new
        @srv.handle(service)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        req = EchoMsg.new
        stub = EchoStub.new(@alt_host, :this_channel_is_insecure)
        op = stub.an_rpc(req, metadata: { k1: 'v1', k2: 'v2' }, return_op: true)
        expect(op.metadata).to be nil
        expect(op.execute).to be_a(EchoMsg)
        wanted_md = {
          'k1' => 'v1',
          'k2' => 'v2',
          'method' => '/EchoService/an_rpc',
          'connect_k1' => 'connect_v1'
        }
        wanted_md.each do |key, value|
          expect(op.metadata[key]).to eq(value)
        end
        @srv.stop
        t.join
      end
    end

    context 'with trailing metadata' do
      before(:each) do
        server_opts = {
          completion_queue_override: @server_queue,
          poll_period: 1
        }
        @srv = RpcServer.new(**server_opts)
        alt_port = @srv.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
        @alt_host = "0.0.0.0:#{alt_port}"
      end

      it 'should be added to BadStatus when requests fail', server: true do
        service = FailingService.new
        @srv.handle(service)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        req = EchoMsg.new
        stub = FailingStub.new(@alt_host, :this_channel_is_insecure)
        blk = proc { stub.an_rpc(req) }

        # confirm it raise the expected error
        expect(&blk).to raise_error GRPC::BadStatus

        # call again and confirm exception contained the trailing metadata.
        begin
          blk.call
        rescue GRPC::BadStatus => e
          expect(e.code).to eq(service.code)
          expect(e.details).to eq(service.details)
          expect(e.metadata).to eq(service.md)
        end
        @srv.stop
        t.join
      end

      it 'should be received by the client', server: true do
        wanted_trailers = { 'k1' => 'out_v1', 'k2' => 'out_v2' }
        service = EchoService.new(k1: 'out_v1', k2: 'out_v2')
        @srv.handle(service)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        req = EchoMsg.new
        stub = EchoStub.new(@alt_host, :this_channel_is_insecure)
        op = stub.an_rpc(req, return_op: true, metadata: { k1: 'v1', k2: 'v2' })
        expect(op.metadata).to be nil
        expect(op.execute).to be_a(EchoMsg)
        expect(op.metadata).to eq(wanted_trailers)
        @srv.stop
        t.join
      end
    end
  end
end
