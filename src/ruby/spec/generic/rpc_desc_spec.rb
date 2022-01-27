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
require 'grpc/generic/rpc_desc'

describe GRPC::RpcDesc do
  RpcDesc = GRPC::RpcDesc
  Stream = RpcDesc::Stream
  OK = GRPC::Core::StatusCodes::OK
  INTERNAL = GRPC::Core::StatusCodes::INTERNAL
  UNKNOWN = GRPC::Core::StatusCodes::UNKNOWN
  CallError = GRPC::Core::CallError

  before(:each) do
    @request_response = RpcDesc.new('rr', Object.new, Object.new, 'encode',
                                    'decode')
    @client_streamer = RpcDesc.new('cs', Stream.new(Object.new), Object.new,
                                   'encode', 'decode')
    @server_streamer = RpcDesc.new('ss', Object.new, Stream.new(Object.new),
                                   'encode', 'decode')
    @bidi_streamer = RpcDesc.new('ss', Stream.new(Object.new),
                                 Stream.new(Object.new), 'encode', 'decode')
    @bs_code = INTERNAL
    @ok_response = Object.new
  end

  shared_examples 'it handles errors' do
    it 'sends the specified status if BadStatus is raised' do
      expect(@call).to receive(:read_unary_request).once.and_return(Object.new)
      expect(@call).to receive(:send_status).once.with(@bs_code, 'NOK', false,
                                                       metadata: {})
      this_desc.run_server_method(@call, method(:bad_status))
    end

    it 'sends status UNKNOWN if other StandardErrors are raised' do
      expect(@call).to receive(:read_unary_request).once.and_return(Object.new)
      expect(@call).to receive(:send_status).once.with(UNKNOWN,
                                                       arg_error_msg,
                                                       false, metadata: {})
      this_desc.run_server_method(@call, method(:other_error))
    end

    it 'sends status UNKNOWN if NotImplementedErrors are raised' do
      expect(@call).to receive(:read_unary_request).once.and_return(Object.new)
      expect(@call).to receive(:send_status).once.with(
        UNKNOWN, not_implemented_error_msg, false, metadata: {})
      this_desc.run_server_method(@call, method(:not_implemented))
    end

    it 'absorbs CallError with no further action' do
      expect(@call).to receive(:read_unary_request).once.and_raise(CallError)
      blk = proc do
        this_desc.run_server_method(@call, method(:fake_reqresp))
      end
      expect(&blk).to_not raise_error
    end
  end

  describe '#run_server_method' do
    let(:fake_md) { { k1: 'v1', k2: 'v2' } }
    describe 'for request responses' do
      let(:this_desc) { @request_response }
      before(:each) do
        @call = double('active_call')
        allow(@call).to receive(:single_req_view).and_return(@call)
        allow(@call).to receive(:output_metadata).and_return(@call)
      end

      it_behaves_like 'it handles errors'

      it 'sends a response and closes the stream if there no errors' do
        req = Object.new
        expect(@call).to receive(:read_unary_request).once.and_return(req)
        expect(@call).to receive(:output_metadata).once.and_return(fake_md)
        expect(@call).to receive(:server_unary_response).once
          .with(@ok_response, trailing_metadata: fake_md)

        this_desc.run_server_method(@call, method(:fake_reqresp))
      end
    end

    describe 'for client streamers' do
      before(:each) do
        @call = double('active_call')
        allow(@call).to receive(:multi_req_view).and_return(@call)
      end

      it 'sends the specified status if BadStatus is raised' do
        expect(@call).to receive(:send_status).once.with(@bs_code, 'NOK', false,
                                                         metadata: {})
        @client_streamer.run_server_method(@call, method(:bad_status_alt))
      end

      it 'sends status UNKNOWN if other StandardErrors are raised' do
        expect(@call).to receive(:send_status).once.with(UNKNOWN, arg_error_msg,
                                                         false, metadata: {})
        @client_streamer.run_server_method(@call, method(:other_error_alt))
      end

      it 'sends status UNKNOWN if NotImplementedErrors are raised' do
        expect(@call).to receive(:send_status).once.with(
          UNKNOWN, not_implemented_error_msg, false, metadata: {})
        @client_streamer.run_server_method(@call, method(:not_implemented_alt))
      end

      it 'absorbs CallError with no further action' do
        expect(@call).to receive(:server_unary_response).once.and_raise(
          CallError)
        allow(@call).to receive(:output_metadata).and_return({})
        blk = proc do
          @client_streamer.run_server_method(@call, method(:fake_clstream))
        end
        expect(&blk).to_not raise_error
      end

      it 'sends a response and closes the stream if there no errors' do
        expect(@call).to receive(:output_metadata).and_return(
          fake_md)
        expect(@call).to receive(:server_unary_response).once
          .with(@ok_response, trailing_metadata: fake_md)

        @client_streamer.run_server_method(@call, method(:fake_clstream))
      end
    end

    describe 'for server streaming' do
      let(:this_desc) { @request_response }
      before(:each) do
        @call = double('active_call')
        allow(@call).to receive(:single_req_view).and_return(@call)
      end

      it_behaves_like 'it handles errors'

      it 'sends a response and closes the stream if there no errors' do
        req = Object.new
        expect(@call).to receive(:read_unary_request).once.and_return(req)
        expect(@call).to receive(:remote_send).twice.with(@ok_response)
        expect(@call).to receive(:output_metadata).and_return(fake_md)
        expect(@call).to receive(:send_status).once.with(OK, 'OK', true,
                                                         metadata: fake_md)
        @server_streamer.run_server_method(@call, method(:fake_svstream))
      end
    end

    describe 'for bidi streamers' do
      before(:each) do
        @call = double('active_call')
        enq_th, rwl_th = double('enqueue_th'), ('read_write_loop_th')
        allow(enq_th).to receive(:join)
        allow(rwl_th).to receive(:join)
      end

      it 'sends the specified status if BadStatus is raised' do
        e = GRPC::BadStatus.new(@bs_code, 'NOK')
        expect(@call).to receive(:run_server_bidi).and_raise(e)
        expect(@call).to receive(:send_status).once.with(@bs_code, 'NOK', false,
                                                         metadata: {})
        @bidi_streamer.run_server_method(@call, method(:bad_status_alt))
      end

      it 'sends status UNKNOWN if other StandardErrors are raised' do
        error_msg = arg_error_msg(StandardError.new)
        expect(@call).to receive(:run_server_bidi).and_raise(StandardError)
        expect(@call).to receive(:send_status).once.with(UNKNOWN, error_msg,
                                                         false, metadata: {})
        @bidi_streamer.run_server_method(@call, method(:other_error_alt))
      end

      it 'sends status UNKNOWN if NotImplementedErrors are raised' do
        expect(@call).to receive(:run_server_bidi).and_raise(
          not_implemented_error)
        expect(@call).to receive(:send_status).once.with(
          UNKNOWN, not_implemented_error_msg, false, metadata: {})
        @bidi_streamer.run_server_method(@call, method(:not_implemented_alt))
      end

      it 'closes the stream if there no errors' do
        expect(@call).to receive(:run_server_bidi)
        expect(@call).to receive(:output_metadata).and_return(fake_md)
        expect(@call).to receive(:send_status).once.with(OK, 'OK', true,
                                                         metadata: fake_md)
        @bidi_streamer.run_server_method(@call, method(:fake_bidistream))
      end
    end
  end

  describe '#assert_arity_matches' do
    def no_arg
    end

    def fake_clstream(_arg)
    end

    def fake_svstream(_arg1, _arg2)
    end

    def fake_three_args(_arg1, _arg2, _arg3)
    end

    it 'raises when a request_response does not have 2 args' do
      [:fake_clstream, :no_arg].each do |mth|
        blk = proc do
          @request_response.assert_arity_matches(method(mth))
        end
        expect(&blk).to raise_error
      end
    end

    it 'passes when a request_response has 2 args' do
      blk = proc do
        @request_response.assert_arity_matches(method(:fake_svstream))
      end
      expect(&blk).to_not raise_error
    end

    it 'raises when a server_streamer does not have 2 args' do
      [:fake_clstream, :no_arg].each do |mth|
        blk = proc do
          @server_streamer.assert_arity_matches(method(mth))
        end
        expect(&blk).to raise_error
      end
    end

    it 'passes when a server_streamer has 2 args' do
      blk = proc do
        @server_streamer.assert_arity_matches(method(:fake_svstream))
      end
      expect(&blk).to_not raise_error
    end

    it 'raises when a client streamer does not have 1 arg' do
      [:fake_svstream, :no_arg].each do |mth|
        blk = proc do
          @client_streamer.assert_arity_matches(method(mth))
        end
        expect(&blk).to raise_error
      end
    end

    it 'passes when a client_streamer has 1 arg' do
      blk = proc do
        @client_streamer.assert_arity_matches(method(:fake_clstream))
      end
      expect(&blk).to_not raise_error
    end

    it 'raises when a bidi streamer does not have 1 or 2 args' do
      [:fake_three_args, :no_arg].each do |mth|
        blk = proc do
          @bidi_streamer.assert_arity_matches(method(mth))
        end
        expect(&blk).to raise_error
      end
    end

    it 'passes when a bidi streamer has 1 arg' do
      blk = proc do
        @bidi_streamer.assert_arity_matches(method(:fake_clstream))
      end
      expect(&blk).to_not raise_error
    end

    it 'passes when a bidi streamer has 2 args' do
      blk = proc do
        @bidi_streamer.assert_arity_matches(method(:fake_svstream))
      end
      expect(&blk).to_not raise_error
    end
  end

  describe '#request_response?' do
    it 'is true only input and output are both not Streams' do
      expect(@request_response.request_response?).to be(true)
      expect(@client_streamer.request_response?).to be(false)
      expect(@bidi_streamer.request_response?).to be(false)
      expect(@server_streamer.request_response?).to be(false)
    end
  end

  describe '#client_streamer?' do
    it 'is true only when input is a Stream and output is not a Stream' do
      expect(@client_streamer.client_streamer?).to be(true)
      expect(@request_response.client_streamer?).to be(false)
      expect(@server_streamer.client_streamer?).to be(false)
      expect(@bidi_streamer.client_streamer?).to be(false)
    end
  end

  describe '#server_streamer?' do
    it 'is true only when output is a Stream and input is not a Stream' do
      expect(@server_streamer.server_streamer?).to be(true)
      expect(@client_streamer.server_streamer?).to be(false)
      expect(@request_response.server_streamer?).to be(false)
      expect(@bidi_streamer.server_streamer?).to be(false)
    end
  end

  describe '#bidi_streamer?' do
    it 'is true only when output is a Stream and input is a Stream' do
      expect(@bidi_streamer.bidi_streamer?).to be(true)
      expect(@server_streamer.bidi_streamer?).to be(false)
      expect(@client_streamer.bidi_streamer?).to be(false)
      expect(@request_response.bidi_streamer?).to be(false)
    end
  end

  def fake_reqresp(_req, _call)
    @ok_response
  end

  def fake_clstream(_call)
    @ok_response
  end

  def fake_svstream(_req, _call)
    [@ok_response, @ok_response]
  end

  def fake_bidistream(an_array)
    an_array
  end

  def bad_status(_req, _call)
    fail GRPC::BadStatus.new(@bs_code, 'NOK')
  end

  def other_error(_req, _call)
    fail(ArgumentError, 'other error')
  end

  def bad_status_alt(_call)
    fail GRPC::BadStatus.new(@bs_code, 'NOK')
  end

  def other_error_alt(_call)
    fail(ArgumentError, 'other error')
  end

  def not_implemented(_req, _call)
    fail not_implemented_error
  end

  def not_implemented_alt(_call)
    fail not_implemented_error
  end

  def arg_error_msg(error = nil)
    error ||= ArgumentError.new('other error')
    "#{error.class}: #{error.message}"
  end

  def not_implemented_error
    NotImplementedError.new('some OS feature not implemented')
  end

  def not_implemented_error_msg(error = nil)
    error ||= not_implemented_error
    "#{error.class}: #{error.message}"
  end
end
