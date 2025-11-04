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
require 'spec_helper'

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

# A test service with no methods.
class EmptyService
  include GRPC::GenericService
end

# A test service without an implementation.
class NoRpcImplementation
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg
end

# A test service with an implementation that fails with BadStatus
class FailingService
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg
  attr_reader :details, :code, :md

  def initialize(_default_var = 'ignored')
    @details = 'app error'
    @code = 3
    @md = { 'failed_method' => 'an_rpc' }
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
  rpc :a_server_streaming_rpc, EchoMsg, stream(EchoMsg)
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

  def a_server_streaming_rpc(_, call)
    GRPC.logger.info("starting a slow #{@delay} server streaming rpc")
    sleep @delay
    @received_md << call.metadata unless call.metadata.nil?
    [EchoMsg.new, EchoMsg.new]
  end
end

SlowStub = SlowService.rpc_stub_class

# A test service that allows a synchronized RPC cancellation
class SynchronizedCancellationService
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg
  attr_reader :received_md, :delay

  # notify_request_received and wait_until_rpc_cancelled are
  # callbacks to synchronously allow the client to proceed with
  # cancellation (after the unary request has been received),
  # and to synchronously wait until the client has cancelled the
  # current RPC.
  def initialize(notify_request_received, wait_until_rpc_cancelled)
    @notify_request_received = notify_request_received
    @wait_until_rpc_cancelled = wait_until_rpc_cancelled
  end

  def an_rpc(req, _call)
    GRPC.logger.info('starting a synchronusly cancelled rpc')
    @notify_request_received.call(req)
    @wait_until_rpc_cancelled.call
    req  # send back the req as the response
  end
end

SynchronizedCancellationStub = SynchronizedCancellationService.rpc_stub_class

# a test service that holds onto call objects
# and uses them after the server-side call has been
# finished
class CheckCallAfterFinishedService
  include GRPC::GenericService
  rpc :an_rpc, EchoMsg, EchoMsg
  rpc :a_client_streaming_rpc, stream(EchoMsg), EchoMsg
  rpc :a_server_streaming_rpc, EchoMsg, stream(EchoMsg)
  rpc :a_bidi_rpc, stream(EchoMsg), stream(EchoMsg)
  attr_reader :server_side_call

  def an_rpc(req, call)
    fail 'shouldnt reuse service' unless @server_side_call.nil?
    @server_side_call = call
    req
  end

  def a_client_streaming_rpc(call)
    fail 'shouldnt reuse service' unless @server_side_call.nil?
    @server_side_call = call
    # iterate through requests so call can complete
    call.each_remote_read.each { |r| GRPC.logger.info(r) }
    EchoMsg.new
  end

  def a_server_streaming_rpc(_, call)
    fail 'shouldnt reuse service' unless @server_side_call.nil?
    @server_side_call = call
    [EchoMsg.new, EchoMsg.new]
  end

  def a_bidi_rpc(requests, call)
    fail 'shouldnt reuse service' unless @server_side_call.nil?
    @server_side_call = call
    requests.each { |r| GRPC.logger.info(r) }
    [EchoMsg.new, EchoMsg.new]
  end
end

CheckCallAfterFinishedServiceStub = CheckCallAfterFinishedService.rpc_stub_class

# A service with a bidi streaming method.
class BidiService
  include GRPC::GenericService
  rpc :server_sends_bad_input, stream(EchoMsg), stream(EchoMsg)

  def server_sends_bad_input(_, _)
    'bad response. (not an enumerable, client sees an error)'
  end
end

BidiStub = BidiService.rpc_stub_class

describe GRPC::RpcServer do
  RpcServer = GRPC::RpcServer
  StatusCodes = GRPC::Core::StatusCodes

  before(:each) do
    @method = 'an_rpc_method'
    @pass = 0
    @fail = 1
    @noop = proc { |x| x }
  end

  describe '#new' do
    it 'can be created with just some args' do
      opts = { server_args: { a_channel_arg: 'an_arg' } }
      blk = proc do
        new_rpc_server_for_testing(**opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'cannot be created with invalid ServerCredentials' do
      blk = proc do
        opts = {
          server_args: { a_channel_arg: 'an_arg' },
          creds: Object.new
        }
        new_rpc_server_for_testing(**opts)
      end
      expect(&blk).to raise_error
    end
  end

  describe '#stopped?' do
    before(:each) do
      opts = { server_args: { a_channel_arg: 'an_arg' }, poll_period: 1.5 }
      @srv = new_rpc_server_for_testing(**opts)
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
      r = new_rpc_server_for_testing(**opts)
      expect(r.running?).to be(false)
    end

    it 'is false if run is called with no services registered', server: true do
      opts = {
        server_args: { a_channel_arg: 'an_arg' },
        poll_period: 2
      }
      r = new_rpc_server_for_testing(**opts)
      r.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
      expect { r.run }.to raise_error(RuntimeError)
    end

    it 'is true after run is called with a registered service' do
      opts = {
        server_args: { a_channel_arg: 'an_arg' },
        poll_period: 2.5
      }
      r = new_rpc_server_for_testing(**opts)
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
      @srv = new_rpc_server_for_testing(**@opts)
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
          poll_period: 1
        }
        @srv = new_rpc_server_for_testing(**server_opts)
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
          stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure,
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
          stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure,
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

      it 'should return UNIMPLEMENTED on unimplemented ' \
         'methods for client_streamer', server: true do
        @srv.handle(EchoService)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        blk = proc do
          stub = EchoStub.new(@host, :this_channel_is_insecure, **client_opts)
          requests = [EchoMsg.new, EchoMsg.new]
          stub.a_client_streaming_rpc_unimplemented(requests)
        end

        begin
          expect(&blk).to raise_error do |error|
            expect(error).to be_a(GRPC::BadStatus)
            expect(error.code).to eq(GRPC::Core::StatusCodes::UNIMPLEMENTED)
          end
        ensure
          @srv.stop # should be call not to crash
          t.join
        end
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

      it 'should raise DeadlineExceeded', server: true do
        service = SlowService.new
        @srv.handle(service)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        req = EchoMsg.new
        stub = SlowStub.new(@host, :this_channel_is_insecure, **client_opts)
        timeout = service.delay - 0.1
        deadline = GRPC::Core::TimeConsts.from_relative_time(timeout)
        responses = stub.a_server_streaming_rpc(req,
                                                deadline: deadline,
                                                metadata: { k1: 'v1', k2: 'v2' })
        expect { responses.to_a }.to raise_error(GRPC::DeadlineExceeded)
        @srv.stop
        t.join
      end

      it 'should handle cancellation correctly', server: true do
        request_received = false
        request_received_mu = Mutex.new
        request_received_cv = ConditionVariable.new
        notify_request_received = proc do |req|
          request_received_mu.synchronize do
            fail 'req is nil' if req.nil?
            expect(req.is_a?(EchoMsg)).to be true
            fail 'test bug - already set' if request_received
            request_received = true
            request_received_cv.signal
          end
        end

        rpc_cancelled = false
        rpc_cancelled_mu = Mutex.new
        rpc_cancelled_cv = ConditionVariable.new
        wait_until_rpc_cancelled = proc do
          rpc_cancelled_mu.synchronize do
            loop do
              break if rpc_cancelled
              rpc_cancelled_cv.wait(rpc_cancelled_mu)
            end
          end
        end

        service = SynchronizedCancellationService.new(notify_request_received,
                                                      wait_until_rpc_cancelled)
        @srv.handle(service)
        srv_thd = Thread.new { @srv.run }
        @srv.wait_till_running
        req = EchoMsg.new
        stub = SynchronizedCancellationStub.new(@host,
                                                :this_channel_is_insecure,
                                                **client_opts)
        op = stub.an_rpc(req, return_op: true)

        client_thd = Thread.new do
          expect { op.execute }.to raise_error GRPC::Cancelled
        end

        request_received_mu.synchronize do
          loop do
            break if request_received
            request_received_cv.wait(request_received_mu)
          end
        end

        op.cancel

        rpc_cancelled_mu.synchronize do
          fail 'test bug - already set' if rpc_cancelled
          rpc_cancelled = true
          rpc_cancelled_cv.signal
        end

        client_thd.join
        @srv.stop
        srv_thd.join
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
          pool_size: 2,
          poll_period: 1,
          max_waiting_requests: 1
        }
        alt_srv = new_rpc_server_for_testing(**opts)
        alt_srv.handle(SlowService)
        alt_port = alt_srv.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
        alt_host = "0.0.0.0:#{alt_port}"
        t = Thread.new { alt_srv.run }
        alt_srv.wait_till_running
        req = EchoMsg.new
        n = 20 # arbitrary, use as many to ensure the server pool is exceeded
        threads = []
        one_failed_as_unavailable = false
        n.times do
          threads << Thread.new do
            stub = SlowStub.new(alt_host, :this_channel_is_insecure)
            begin
              stub.an_rpc(req)
            rescue GRPC::ResourceExhausted
              one_failed_as_unavailable = true
            end
          end
        end
        threads.each(&:join)
        alt_srv.stop
        t.join
        expect(one_failed_as_unavailable).to be(true)
      end

      it 'should send a status UNKNOWN with a relevant message when the' \
        'servers response stream is not an enumerable' do
        @srv.handle(BidiService)
        t = Thread.new { @srv.run }
        @srv.wait_till_running
        stub = BidiStub.new(@host, :this_channel_is_insecure, **client_opts)
        responses = stub.server_sends_bad_input([])
        exception = nil
        begin
          responses.each { |r| r }
        rescue GRPC::Unknown => e
          exception = e
        end
        # Erroneous responses sent from the server handler should cause an
        # exception on the client with relevant info.
        expected_details = 'NoMethodError: undefined method `each\' for '\
          '"bad response. (not an enumerable, client sees an error)"'

        expect(exception.inspect.include?(expected_details)).to be true
        @srv.stop
        t.join
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
          poll_period: 1,
          connect_md_proc: test_md_proc
        }
        @srv = new_rpc_server_for_testing(**server_opts)
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
          GRPC.logger.info("key: #{key}")
          expect(op.metadata[key]).to eq(value)
        end
        @srv.stop
        t.join
      end
    end

    context 'with trailing metadata' do
      before(:each) do
        server_opts = {
          poll_period: 1
        }
        @srv = new_rpc_server_for_testing(**server_opts)
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
        expect(op.trailing_metadata).to eq(wanted_trailers)
        @srv.stop
        t.join
      end
    end

    context 'when call objects are used after calls have completed' do
      before(:each) do
        server_opts = {
          poll_period: 1
        }
        @srv = new_rpc_server_for_testing(**server_opts)
        alt_port = @srv.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
        @alt_host = "0.0.0.0:#{alt_port}"

        @service = CheckCallAfterFinishedService.new
        @srv.handle(@service)
        @srv_thd  = Thread.new { @srv.run }
        @srv.wait_till_running
      end

      # check that the server-side call is still in a usable state even
      # after it has finished
      def check_single_req_view_of_finished_call(call)
        common_check_of_finished_server_call(call)

        expect(call.peer).to be_a(String)
        expect(call.peer_cert).to be(nil)
      end

      def check_multi_req_view_of_finished_call(call)
        common_check_of_finished_server_call(call)

        l = []
        call.each_remote_read.each { |r| l << r }
        expect(l.size).to eq(0)
      end

      def common_check_of_finished_server_call(call)
        expect do
          call.merge_metadata_to_send({})
        end.to raise_error(RuntimeError)

        expect do
          call.send_initial_metadata
        end.to_not raise_error

        expect(call.cancelled?).to be(false)
        expect(call.metadata).to be_a(Hash)
        expect(call.metadata['user-agent']).to be_a(String)

        expect(call.metadata_sent).to be(true)
        expect(call.output_metadata).to eq({})
        expect(call.metadata_to_send).to eq({})
        expect(call.deadline.is_a?(Time)).to be(true)
      end

      it 'should not crash when call used after an unary call is finished' do
        req = EchoMsg.new
        stub = CheckCallAfterFinishedServiceStub.new(@alt_host,
                                                     :this_channel_is_insecure)
        resp = stub.an_rpc(req)
        expect(resp).to be_a(EchoMsg)
        @srv.stop
        @srv_thd.join

        check_single_req_view_of_finished_call(@service.server_side_call)
      end

      it 'should not crash when call used after client streaming finished' do
        requests = [EchoMsg.new, EchoMsg.new]
        stub = CheckCallAfterFinishedServiceStub.new(@alt_host,
                                                     :this_channel_is_insecure)
        resp = stub.a_client_streaming_rpc(requests)
        expect(resp).to be_a(EchoMsg)
        @srv.stop
        @srv_thd.join

        check_multi_req_view_of_finished_call(@service.server_side_call)
      end

      it 'should not crash when call used after server streaming finished' do
        req = EchoMsg.new
        stub = CheckCallAfterFinishedServiceStub.new(@alt_host,
                                                     :this_channel_is_insecure)
        responses = stub.a_server_streaming_rpc(req)
        responses.each do |r|
          expect(r).to be_a(EchoMsg)
        end
        @srv.stop
        @srv_thd.join

        check_single_req_view_of_finished_call(@service.server_side_call)
      end

      it 'should not crash when call used after a bidi call is finished' do
        requests = [EchoMsg.new, EchoMsg.new]
        stub = CheckCallAfterFinishedServiceStub.new(@alt_host,
                                                     :this_channel_is_insecure)
        responses = stub.a_bidi_rpc(requests)
        responses.each do |r|
          expect(r).to be_a(EchoMsg)
        end
        @srv.stop
        @srv_thd.join

        check_multi_req_view_of_finished_call(@service.server_side_call)
      end
    end
  end
end
