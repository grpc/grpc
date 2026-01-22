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

Thread.abort_on_exception = true

def wakey_thread(&blk)
  n = GRPC::Notifier.new
  t = Thread.new do
    blk.call(n)
  end
  t.abort_on_exception = true
  n.wait
  t
end

def load_test_certs
  test_root = File.join(File.dirname(File.dirname(__FILE__)), 'testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(test_root, f)).read }
end

include GRPC::Core::StatusCodes
include GRPC::Core::TimeConsts
include GRPC::Core::CallOps

# check that methods on a finished/closed call t crash
def check_op_view_of_finished_client_call(op_view,
                                          expected_metadata,
                                          expected_trailing_metadata)
  # use read_response_stream to try to iterate through
  # possible response stream
  fail('need something to attempt reads') unless block_given?
  expect do
    resp = op_view.execute
    yield resp
  end.to raise_error(GRPC::Core::CallError)

  expect { op_view.start_call }.to raise_error(RuntimeError)

  sanity_check_values_of_accessors(op_view,
                                   expected_metadata,
                                   expected_trailing_metadata)

  expect do
    op_view.wait
    op_view.cancel
    op_view.write_flag = 1
  end.to_not raise_error
end

def sanity_check_values_of_accessors(op_view,
                                     expected_metadata,
                                     expected_trailing_metadata)
  expect(op_view.status.code).to eq(0)
  expect(op_view.status.metadata).to eq(expected_trailing_metadata)
  expect(op_view.metadata).to eq(expected_metadata)
  expect(op_view.trailing_metadata).to eq(expected_trailing_metadata)

  expect(op_view.cancelled?).to be(false)
  expect(op_view.write_flag).to be(nil)

  # The deadline attribute of a call can be either
  # a GRPC::Core::TimeSpec or a Time, which are mutually exclusive.
  # TODO: fix so that the accessor always returns the same type.
  expect(op_view.deadline.is_a?(GRPC::Core::TimeSpec) ||
         op_view.deadline.is_a?(Time)).to be(true)
end

def close_active_server_call(active_server_call)
  active_server_call.send(:set_input_stream_done)
  active_server_call.send(:set_output_stream_done)
end

describe 'ClientStub' do  # rubocop:disable Metrics/BlockLength
  let(:noop) { proc { |x| x } }

  before(:each) do
    Thread.abort_on_exception = true
    @server = nil
    @method = 'an_rpc_method'
    @pass = OK
    @fail = INTERNAL
    @metadata = { k1: 'v1', k2: 'v2' }
  end

  after(:each) do
    unless @server.nil?
      @server.shutdown_and_notify(from_relative_time(2))
      @server.close
    end
  end

  describe '#new' do
    let(:fake_host) { 'localhost:0' }
    it 'can be created from a host and args' do
      opts = { channel_args: { a_channel_arg: 'an_arg' } }
      blk = proc do
        GRPC::ClientStub.new(fake_host, :this_channel_is_insecure, **opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'can be created with an channel override' do
      opts = {
        channel_args: { a_channel_arg: 'an_arg' },
        channel_override: @ch
      }
      blk = proc do
        GRPC::ClientStub.new(fake_host, :this_channel_is_insecure, **opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'cannot be created with a bad channel override' do
      blk = proc do
        opts = {
          channel_args: { a_channel_arg: 'an_arg' },
          channel_override: Object.new
        }
        GRPC::ClientStub.new(fake_host, :this_channel_is_insecure, **opts)
      end
      expect(&blk).to raise_error
    end

    it 'cannot be created with bad credentials' do
      blk = proc do
        opts = { channel_args: { a_channel_arg: 'an_arg' } }
        GRPC::ClientStub.new(fake_host, Object.new, **opts)
      end
      expect(&blk).to raise_error
    end

    it 'can be created with test test credentials' do
      certs = load_test_certs
      blk = proc do
        opts = {
          channel_args: {
            GRPC::Core::Channel::SSL_TARGET => 'foo.test.google.fr',
            a_channel_arg: 'an_arg'
          }
        }
        creds = GRPC::Core::ChannelCredentials.new(certs[0], nil, nil)
        GRPC::ClientStub.new(fake_host, creds,  **opts)
      end
      expect(&blk).to_not raise_error
    end
  end

  describe '#request_response', request_response: true do
    before(:each) do
      @sent_msg, @resp = 'a_msg', 'a_reply'
    end

    shared_examples 'request response' do
      it 'should send a request to/receive a reply from a server' do
        server_port = create_test_server
        th = run_request_response(@sent_msg, @resp, @pass)
        stub = GRPC::ClientStub.new("localhost:#{server_port}",
                                    :this_channel_is_insecure)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      def metadata_test(md)
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @pass,
                                  expected_metadata: md)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        @metadata = md
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should send metadata to the server ok' do
        metadata_test(k1: 'v1', k2: 'v2')
      end

      # these tests mostly try to exercise when md might be allocated
      # instead of inlined
      it 'should send metadata with multiple large md to the server ok' do
        val_array = %w(
          '00000000000000000000000000000000000000000000000000000000000000',
          '11111111111111111111111111111111111111111111111111111111111111',
          '22222222222222222222222222222222222222222222222222222222222222',
        )
        md = {
          k1: val_array,
          k2: 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',
          k3: 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb',
          k4: 'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
          keeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeey5: 'v5',
          'k66666666666666666666666666666666666666666666666666666' => 'v6',
          'k77777777777777777777777777777777777777777777777777777' => 'v7',
          'k88888888888888888888888888888888888888888888888888888' => 'v8'
        }
        metadata_test(md)
      end

      it 'should send a request when configured using an override channel' do
        server_port = create_test_server
        alt_host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @pass)
        ch = GRPC::Core::Channel.new(alt_host, nil, :this_channel_is_insecure)
        stub = GRPC::ClientStub.new('ignored-host',
                                    :this_channel_is_insecure,
                                    channel_override: ch)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should raise an error if the status is not OK' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @fail)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        blk = proc { get_response(stub) }
        expect(&blk).to raise_error(GRPC::BadStatus)
        th.join
      end

      it 'should receive UNAVAILABLE if call credentials plugin fails' do
        server_port = create_secure_test_server
        server_started_notifier = GRPC::Notifier.new
        th = Thread.new do
          @server.start
          server_started_notifier.notify(nil)
          # Poll on the server so that the client connection can proceed.
          # We don't expect the server to actually accept a call though.
          expect { @server.request_call }.to raise_error(GRPC::Core::CallError)
        end
        server_started_notifier.wait

        certs = load_test_certs
        secure_channel_creds = GRPC::Core::ChannelCredentials.new(
          certs[0], nil, nil)
        secure_stub_opts = {
          channel_args: {
            GRPC::Core::Channel::SSL_TARGET => 'foo.test.google.fr'
          }
        }
        stub = GRPC::ClientStub.new("localhost:#{server_port}",
                                    secure_channel_creds, **secure_stub_opts)

        error_message = 'Failing call credentials callback'
        failing_auth = proc do
          fail error_message
        end
        creds = GRPC::Core::CallCredentials.new(failing_auth)

        unavailable_error_occurred = false
        begin
          get_response(stub, credentials: creds)
        rescue GRPC::Unavailable => e
          unavailable_error_occurred = true
          expect(e.details.include?(error_message)).to be true
        end
        expect(unavailable_error_occurred).to eq(true)

        @server.shutdown_and_notify(Time.now + 3)
        th.join
        @server.close
      end

      it 'should raise ArgumentError if metadata contains invalid values' do
        @metadata.merge!(k3: 3)
        server_port = create_test_server
        host = "localhost:#{server_port}"
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        expect do
          get_response(stub)
        end.to raise_error(ArgumentError,
                           /Header values must be of type string or array/)
      end
    end

    describe 'without a call operation' do
      def get_response(stub, credentials: nil)
        GRPC.logger.info(credentials.inspect)
        stub.request_response(@method, @sent_msg, noop, noop,
                              metadata: @metadata,
                              credentials: credentials)
      end

      it_behaves_like 'request response'
    end

    describe 'via a call operation' do
      after(:each) do
        # make sure op.wait doesn't freeze, even if there's a bad status
        @op.wait
      end
      def get_response(stub, run_start_call_first: false, credentials: nil)
        @op = stub.request_response(@method, @sent_msg, noop, noop,
                                    return_op: true,
                                    metadata: @metadata,
                                    deadline: from_relative_time(2),
                                    credentials: credentials)
        expect(@op).to be_a(GRPC::ActiveCall::Operation)
        @op.start_call if run_start_call_first
        result = @op.execute
        result
      end

      it_behaves_like 'request response'

      def run_op_view_metadata_test(run_start_call_first)
        server_port = create_test_server
        host = "localhost:#{server_port}"

        @server_initial_md = { 'sk1' => 'sv1', 'sk2' => 'sv2' }
        @server_trailing_md = { 'tk1' => 'tv1', 'tk2' => 'tv2' }
        th = run_request_response(
          @sent_msg, @resp, @pass,
          expected_metadata: @metadata,
          server_initial_md: @server_initial_md,
          server_trailing_md: @server_trailing_md)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        expect(
          get_response(stub,
                       run_start_call_first: run_start_call_first)).to eq(@resp)
        th.join
      end

      it 'sends metadata to the server ok when running start_call first' do
        run_op_view_metadata_test(true)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md
        ) { |r| GRPC.logger.info(r) }
      end

      it 'does not crash when used after the call has been finished' do
        run_op_view_metadata_test(false)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md
        ) { |r| GRPC.logger.info(r) }
      end
    end
  end

  describe '#client_streamer', client_streamer: true do
    before(:each) do
      Thread.abort_on_exception = true
      server_port = create_test_server
      host = "localhost:#{server_port}"
      @stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
      @sent_msgs = Array.new(3) { |i| 'msg_' + (i + 1).to_s }
      @resp = 'a_reply'
    end

    shared_examples 'client streaming' do
      it 'should send requests to/receive a reply from a server' do
        th = run_client_streamer(@sent_msgs, @resp, @pass)
        expect(get_response(@stub)).to eq(@resp)
        th.join
      end

      it 'should send metadata to the server ok' do
        th = run_client_streamer(@sent_msgs, @resp, @pass,
                                 expected_metadata: @metadata)
        expect(get_response(@stub)).to eq(@resp)
        th.join
      end

      it 'should raise an error if the status is not ok' do
        th = run_client_streamer(@sent_msgs, @resp, @fail)
        blk = proc { get_response(@stub) }
        expect(&blk).to raise_error(GRPC::BadStatus)
        th.join
      end

      it 'should raise ArgumentError if metadata contains invalid values' do
        @metadata.merge!(k3: 3)
        expect do
          get_response(@stub)
        end.to raise_error(ArgumentError,
                           /Header values must be of type string or array/)
      end
    end

    describe 'without a call operation' do
      def get_response(stub)
        stub.client_streamer(@method, @sent_msgs, noop, noop,
                             metadata: @metadata)
      end

      it_behaves_like 'client streaming'
    end

    describe 'via a call operation' do
      after(:each) do
        # make sure op.wait doesn't freeze, even if there's a bad status
        @op.wait
      end
      def get_response(stub, run_start_call_first: false)
        @op = stub.client_streamer(@method, @sent_msgs, noop, noop,
                                   return_op: true, metadata: @metadata)
        expect(@op).to be_a(GRPC::ActiveCall::Operation)
        @op.start_call if run_start_call_first
        result = @op.execute
        result
      end

      it_behaves_like 'client streaming'

      def run_op_view_metadata_test(run_start_call_first)
        @server_initial_md = { 'sk1' => 'sv1', 'sk2' => 'sv2' }
        @server_trailing_md = { 'tk1' => 'tv1', 'tk2' => 'tv2' }
        th = run_client_streamer(
          @sent_msgs, @resp, @pass,
          expected_metadata: @metadata,
          server_initial_md: @server_initial_md,
          server_trailing_md: @server_trailing_md)
        expect(
          get_response(@stub,
                       run_start_call_first: run_start_call_first)).to eq(@resp)
        th.join
      end

      it 'sends metadata to the server ok when running start_call first' do
        run_op_view_metadata_test(true)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md
        ) { |r| GRPC.logger.info(r) }
      end

      it 'does not crash when used after the call has been finished' do
        run_op_view_metadata_test(false)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md
        ) { |r| GRPC.logger.info(r) }
      end
    end
  end

  describe '#server_streamer', server_streamer: true do
    before(:each) do
      @sent_msg = 'a_msg'
      @replies = Array.new(3) { |i| 'reply_' + (i + 1).to_s }
    end

    shared_examples 'server streaming' do
      it 'should send a request to/receive replies from a server' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replies, @pass)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        expect(get_responses(stub).collect { |r| r }).to eq(@replies)
        th.join
      end

      it 'should raise an error if the status is not ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replies, @fail)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect { e.collect { |r| r } }.to raise_error(GRPC::BadStatus)
        th.join
      end

      it 'should send metadata to the server ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replies, @fail,
                                 expected_metadata: { k1: 'v1', k2: 'v2' })
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect { e.collect { |r| r } }.to raise_error(GRPC::BadStatus)
        th.join
      end

      it 'should raise ArgumentError if metadata contains invalid values' do
        @metadata.merge!(k3: 3)
        server_port = create_test_server
        host = "localhost:#{server_port}"
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        expect do
          get_responses(stub).collect { |r| r }
        end.to raise_error(ArgumentError,
                           /Header values must be of type string or array/)
      end

      it 'the call terminates when there is an unmarshalling error' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer_handle_client_cancellation(
          @sent_msg, @replies)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)

        unmarshal = proc { fail(ArgumentError, 'test unmarshalling error') }
        expect do
          get_responses(stub, unmarshal: unmarshal).collect { |r| r }
        end.to raise_error(ArgumentError, 'test unmarshalling error')
        th.join
      end
    end

    describe 'without a call operation' do
      def get_responses(stub, unmarshal: noop)
        e = stub.server_streamer(@method, @sent_msg, noop, unmarshal,
                                 metadata: @metadata)
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'server streaming'
    end

    describe 'via a call operation' do
      after(:each) do
        @op.wait # make sure wait doesn't freeze
      end
      def get_responses(stub, run_start_call_first: false, unmarshal: noop)
        @op = stub.server_streamer(@method, @sent_msg, noop, unmarshal,
                                   return_op: true,
                                   metadata: @metadata)
        expect(@op).to be_a(GRPC::ActiveCall::Operation)
        @op.start_call if run_start_call_first
        e = @op.execute
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'server streaming'

      def run_op_view_metadata_test(run_start_call_first)
        server_port = create_test_server
        host = "localhost:#{server_port}"
        @server_initial_md = { 'sk1' => 'sv1', 'sk2' => 'sv2' }
        @server_trailing_md = { 'tk1' => 'tv1', 'tk2' => 'tv2' }
        th = run_server_streamer(
          @sent_msg, @replies, @pass,
          expected_metadata: @metadata,
          server_initial_md: @server_initial_md,
          server_trailing_md: @server_trailing_md)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        e = get_responses(stub, run_start_call_first: run_start_call_first)
        expect(e.collect { |r| r }).to eq(@replies)
        th.join
      end

      it 'should send metadata to the server ok when start_call is run first' do
        run_op_view_metadata_test(true)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md) do |responses|
          responses.each { |r| GRPC.logger.info(r) }
        end
      end

      it 'does not crash when used after the call has been finished' do
        run_op_view_metadata_test(false)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md) do |responses|
          responses.each { |r| GRPC.logger.info(r) }
        end
      end

      it 'raises GRPC::Cancelled after the call has been cancelled' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer_handle_client_cancellation(
          @sent_msg, @replies)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        resp = get_responses(stub, run_start_call_first: false)
        expect(resp.next).to eq('reply_1')
        @op.cancel
        expect { resp.next }.to raise_error(GRPC::Cancelled)
        th.join
      end
    end
  end

  describe '#bidi_streamer', bidi: true do
    before(:each) do
      @sent_msgs = Array.new(3) { |i| 'msg_' + (i + 1).to_s }
      @replies = Array.new(3) { |i| 'reply_' + (i + 1).to_s }
      server_port = create_test_server
      @host = "localhost:#{server_port}"
    end

    shared_examples 'bidi streaming' do
      it 'supports sending all the requests first' do
        th = run_bidi_streamer_handle_inputs_first(@sent_msgs, @replies,
                                                   @pass)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@replies)
        th.join
      end

      it 'supports client-initiated ping pong' do
        th = run_bidi_streamer_echo_ping_pong(@sent_msgs, @pass, true)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end

      it 'supports a server-initiated ping pong' do
        th = run_bidi_streamer_echo_ping_pong(@sent_msgs, @pass, false)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end

      it 'should raise an error if the status is not ok' do
        th = run_bidi_streamer_echo_ping_pong(@sent_msgs, @fail, false)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect { e.collect { |r| r } }.to raise_error(GRPC::BadStatus)
        th.join
      end

      it 'should raise ArgumentError if metadata contains invalid values' do
        @metadata.merge!(k3: 3)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        expect do
          get_responses(stub).collect { |r| r }
        end.to raise_error(ArgumentError,
                           /Header values must be of type string or array/)
      end

      it 'terminates if the call fails to start' do
        # don't start the server
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        expect do
          get_responses(stub, deadline: from_relative_time(0)).collect { |r| r }
        end.to raise_error(GRPC::BadStatus)
      end

      it 'should send metadata to the server ok' do
        th = run_bidi_streamer_echo_ping_pong(@sent_msgs, @pass, true,
                                              expected_metadata: @metadata)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end

      # Prompted by grpc/github #10526
      describe 'surfacing of errors when sending requests' do
        def run_server_bidi_send_one_then_read_indefinitely
          @server.start
          recvd_rpc = @server.request_call
          recvd_call = recvd_rpc.call
          server_call = GRPC::ActiveCall.new(
            recvd_call, noop, noop, INFINITE_FUTURE,
            metadata_received: true, started: false)
          server_call.send_initial_metadata
          server_call.remote_send('server response')
          loop do
            m = server_call.remote_read
            break if m.nil?
          end
          # can't fail since initial metadata already sent
          server_call.send_status(@pass, 'OK', true)
          close_active_server_call(server_call)
        end

        def verify_error_from_write_thread(stub, requests_to_push,
                                           request_queue, expected_description)
          # TODO: an improvement might be to raise the original exception from
          # bidi call write loops instead of only cancelling the call
          failing_marshal_proc = proc do |req|
            fail req if req.is_a?(StandardError)
            req
          end
          begin
            e = get_responses(stub, marshal_proc: failing_marshal_proc)
            first_response = e.next
            expect(first_response).to eq('server response')
            requests_to_push.each { |req| request_queue.push(req) }
            e.collect { |r| r }
          rescue GRPC::Unknown => e
            exception = e
          end
          expect(exception.message.include?(expected_description)).to be(true)
        end

        # Provides an Enumerable view of a Queue
        class BidiErrorTestingEnumerateForeverQueue
          def initialize(queue)
            @queue = queue
          end

          def each
            loop do
              msg = @queue.pop
              yield msg
            end
          end
        end

        def run_error_in_client_request_stream_test(requests_to_push,
                                                    expected_error_message)
          # start a server that waits on a read indefinitely - it should
          # see a cancellation and be able to break out
          th = Thread.new { run_server_bidi_send_one_then_read_indefinitely }
          stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)

          request_queue = Queue.new
          @sent_msgs = BidiErrorTestingEnumerateForeverQueue.new(request_queue)

          verify_error_from_write_thread(stub,
                                         requests_to_push,
                                         request_queue,
                                         expected_error_message)
          # the write loop error should cancel the call and end the
          # server's request stream
          th.join
        end

        it 'non-GRPC errors from the write loop surface when raised ' \
          'at the start of a request stream' do
          expected_error_message = 'expect error on first request'
          requests_to_push = [StandardError.new(expected_error_message)]
          run_error_in_client_request_stream_test(requests_to_push,
                                                  expected_error_message)
        end

        it 'non-GRPC errors from the write loop surface when raised ' \
          'during the middle of a request stream' do
          expected_error_message = 'expect error on last request'
          requests_to_push = %w( one two )
          requests_to_push << StandardError.new(expected_error_message)
          run_error_in_client_request_stream_test(requests_to_push,
                                                  expected_error_message)
        end
      end

      # Prompted by grpc/github #14853
      describe 'client-side error handling on bidi streams' do
        class EnumeratorQueue
          def initialize(queue)
            @queue = queue
          end

          def each
            loop do
              msg = @queue.pop
              break if msg.nil?
              yield msg
            end
          end
        end

        def run_server_bidi_shutdown_after_one_read
          @server.start
          recvd_rpc = @server.request_call
          recvd_call = recvd_rpc.call
          server_call = GRPC::ActiveCall.new(
            recvd_call, noop, noop, INFINITE_FUTURE,
            metadata_received: true, started: false)
          expect(server_call.remote_read).to eq('first message')
          @server.shutdown_and_notify(from_relative_time(0))
          @server.close
        end

        it 'receives a grpc status code when writes to a bidi stream fail' do
          # This test tries to trigger the case when a 'SEND_MESSAGE' op
          # and subsequent 'SEND_CLOSE_FROM_CLIENT' op of a bidi stream fails.
          # In this case, iteration through the response stream should result
          # in a grpc status code, and the writer thread should not raise an
          # exception.
          server_thread = Thread.new do
            run_server_bidi_shutdown_after_one_read
          end
          stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
          request_queue = Queue.new
          @sent_msgs = EnumeratorQueue.new(request_queue)
          responses = get_responses(stub)
          request_queue.push('first message')
          # Now wait for the server to shut down.
          server_thread.join
          # Sanity check. This test is not interesting if
          # Thread.abort_on_exception is not set.
          expect(Thread.abort_on_exception).to be(true)
          # An attempt to send a second message should fail now that the
          # server is down.
          request_queue.push('second message')
          request_queue.push(nil)
          expect { responses.next }.to raise_error(GRPC::BadStatus)
        end

        def run_server_bidi_shutdown_after_one_write
          @server.start
          recvd_rpc = @server.request_call
          recvd_call = recvd_rpc.call
          server_call = GRPC::ActiveCall.new(
            recvd_call, noop, noop, INFINITE_FUTURE,
            metadata_received: true, started: false)
          server_call.send_initial_metadata
          server_call.remote_send('message')
          @server.shutdown_and_notify(from_relative_time(0))
          @server.close
        end

        it 'receives a grpc status code when reading from a failed bidi call' do
          server_thread = Thread.new do
            run_server_bidi_shutdown_after_one_write
          end
          stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
          request_queue = Queue.new
          @sent_msgs = EnumeratorQueue.new(request_queue)
          responses = get_responses(stub)
          expect(responses.next).to eq('message')
          # Wait for the server to shut down
          server_thread.join
          expect { responses.next }.to raise_error(GRPC::BadStatus)
          # Push a sentinel to allow the writer thread to finish
          request_queue.push(nil)
        end
      end
    end

    describe 'without a call operation' do
      def get_responses(stub, deadline: nil, marshal_proc: noop)
        e = stub.bidi_streamer(@method, @sent_msgs, marshal_proc, noop,
                               metadata: @metadata, deadline: deadline)
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'bidi streaming'
    end

    describe 'via a call operation' do
      after(:each) do
        @op.wait # make sure wait doesn't freeze
      end
      def get_responses(stub, run_start_call_first: false, deadline: nil,
                        marshal_proc: noop)
        @op = stub.bidi_streamer(@method, @sent_msgs, marshal_proc, noop,
                                 return_op: true,
                                 metadata: @metadata, deadline: deadline)
        expect(@op).to be_a(GRPC::ActiveCall::Operation)
        @op.start_call if run_start_call_first
        e = @op.execute
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'bidi streaming'

      def run_op_view_metadata_test(run_start_call_first)
        @server_initial_md = { 'sk1' => 'sv1', 'sk2' => 'sv2' }
        @server_trailing_md = { 'tk1' => 'tv1', 'tk2' => 'tv2' }
        th = run_bidi_streamer_echo_ping_pong(
          @sent_msgs, @pass, true,
          expected_metadata: @metadata,
          server_initial_md: @server_initial_md,
          server_trailing_md: @server_trailing_md)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub, run_start_call_first: run_start_call_first)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end

      it 'can run start_call before executing the call' do
        run_op_view_metadata_test(true)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md) do |responses|
          responses.each { |r| GRPC.logger.info(r) }
        end
      end

      it 'doesnt crash when op_view used after call has finished' do
        run_op_view_metadata_test(false)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md) do |responses|
          responses.each { |r| GRPC.logger.info(r) }
        end
      end

      def run_server_bidi_expect_client_to_cancel(wait_for_shutdown_ok_callback)
        @server.start
        recvd_rpc = @server.request_call
        recvd_call = recvd_rpc.call
        server_call = GRPC::ActiveCall.new(
          recvd_call, noop, noop, INFINITE_FUTURE,
          metadata_received: true, started: false)
        server_call.send_initial_metadata
        server_call.remote_send('server call received')
        wait_for_shutdown_ok_callback.call
        # since the client is cancelling the call,
        # we should be able to shut down cleanly
        @server.shutdown_and_notify(nil)
        @server.close
      end

      it 'receives a grpc status code when reading from a cancelled bidi call' do
        # This test tries to trigger a 'RECV_INITIAL_METADATA' and/or
        # 'RECV_MESSAGE' op failure.
        # An attempt to read a message might fail; in that case, iteration
        # through the response stream should still result in a grpc status.
        server_can_shutdown = false
        server_can_shutdown_mu = Mutex.new
        server_can_shutdown_cv = ConditionVariable.new
        wait_for_shutdown_ok_callback = proc do
          server_can_shutdown_mu.synchronize do
            server_can_shutdown_cv.wait(server_can_shutdown_mu) until server_can_shutdown
          end
        end
        server_thread = Thread.new do
          run_server_bidi_expect_client_to_cancel(wait_for_shutdown_ok_callback)
        end
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        request_queue = Queue.new
        @sent_msgs = EnumeratorQueue.new(request_queue)
        responses = get_responses(stub)
        expect(responses.next).to eq('server call received')
        @op.cancel
        expect { responses.next }.to raise_error(GRPC::Cancelled)
        # Now let the server proceed to shut down.
        server_can_shutdown_mu.synchronize do
          server_can_shutdown = true
          server_can_shutdown_cv.broadcast
        end
        server_thread.join
        # Push a sentinel to allow the writer thread to finish
        request_queue.push(nil)
      end
    end
  end

  def run_server_streamer(expected_input, replies, status,
                          expected_metadata: {},
                          server_initial_md: {},
                          server_trailing_md: {})
    wanted_metadata = expected_metadata.clone
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(
        notifier, metadata_to_send: server_initial_md)
      wanted_metadata.each do |k, v|
        expect(c.metadata[k.to_s]).to eq(v)
      end
      expect(c.remote_read).to eq(expected_input)
      replies.each { |r| c.remote_send(r) }
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true,
                    metadata: server_trailing_md)
      close_active_server_call(c)
    end
  end

  def run_bidi_streamer_handle_inputs_first(expected_inputs, replies,
                                            status)
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(notifier)
      expected_inputs.each { |i| expect(c.remote_read).to eq(i) }
      replies.each { |r| c.remote_send(r) }
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true)
      close_active_server_call(c)
    end
  end

  def run_bidi_streamer_echo_ping_pong(expected_inputs, status, client_starts,
                                       expected_metadata: {},
                                       server_initial_md: {},
                                       server_trailing_md: {})
    wanted_metadata = expected_metadata.clone
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(
        notifier, metadata_to_send: server_initial_md)
      wanted_metadata.each do |k, v|
        expect(c.metadata[k.to_s]).to eq(v)
      end
      expected_inputs.each do |i|
        if client_starts
          expect(c.remote_read).to eq(i)
          c.remote_send(i)
        else
          c.remote_send(i)
          expect(c.remote_read).to eq(i)
        end
      end
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true,
                    metadata: server_trailing_md)
      close_active_server_call(c)
    end
  end

  def run_client_streamer(expected_inputs, resp, status,
                          expected_metadata: {},
                          server_initial_md: {},
                          server_trailing_md: {})
    wanted_metadata = expected_metadata.clone
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(
        notifier, metadata_to_send: server_initial_md)
      expected_inputs.each { |i| expect(c.remote_read).to eq(i) }
      wanted_metadata.each do |k, v|
        expect(c.metadata[k.to_s]).to eq(v)
      end
      c.remote_send(resp)
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true,
                    metadata: server_trailing_md)
      close_active_server_call(c)
    end
  end

  def run_server_streamer_handle_client_cancellation(
    expected_input, replies)
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(notifier)
      expect(c.remote_read).to eq(expected_input)
      begin
        replies.each { |r| c.remote_send(r) }
      rescue GRPC::Core::CallError
        # An attempt to write to the client might fail. This is ok
        # because the client call is expected to cancel the call,
        # and there is a race as for when the server-side call will
        # start to fail.
        p 'remote_send failed (allowed because call expected to cancel)'
      ensure
        c.send_status(OK, 'OK', true)
        close_active_server_call(c)
      end
    end
  end

  def run_request_response(expected_input, resp, status,
                           expected_metadata: {},
                           server_initial_md: {},
                           server_trailing_md: {})
    wanted_metadata = expected_metadata.clone
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(
        notifier, metadata_to_send: server_initial_md)
      expect(c.remote_read).to eq(expected_input)
      wanted_metadata.each do |k, v|
        expect(c.metadata[k.to_s]).to eq(v)
      end
      c.remote_send(resp)
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true,
                    metadata: server_trailing_md)
      close_active_server_call(c)
    end
  end

  def create_secure_test_server
    certs = load_test_certs
    secure_credentials = GRPC::Core::ServerCredentials.new(
      nil, [{ private_key: certs[1], cert_chain: certs[2] }], false)

    @server = new_core_server_for_testing(nil)
    @server.add_http2_port('0.0.0.0:0', secure_credentials)
  end

  def create_test_server
    @server = new_core_server_for_testing(nil)
    @server.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
  end

  def expect_server_to_be_invoked(notifier, metadata_to_send: nil)
    @server.start
    notifier.notify(nil)
    recvd_rpc = @server.request_call
    recvd_call = recvd_rpc.call
    recvd_call.metadata = recvd_rpc.metadata
    recvd_call.run_batch(SEND_INITIAL_METADATA => metadata_to_send)
    GRPC::ActiveCall.new(recvd_call, noop, noop, INFINITE_FUTURE,
                         metadata_received: true)
  end
end
