#!/usr/bin/env ruby

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

# client is a testing tool that accesses a gRPC interop testing server and runs
# a test on it.
#
# Helps validate interoperation b/w different gRPC implementations.
#
# Usage: $ path/to/client.rb --server_host=<hostname> \
#                            --server_port=<port> \
#                            --test_case=<testcase_name>

# These lines are required for the generated files to load grpc
this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(File.dirname(this_dir)), 'lib')
pb_dir = File.dirname(this_dir)
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)
$LOAD_PATH.unshift(pb_dir) unless $LOAD_PATH.include?(pb_dir)

require 'optparse'
require 'logger'

require_relative '../../lib/grpc'
require 'googleauth'
require 'google/protobuf'

require_relative '../src/proto/grpc/testing/empty_pb'
require_relative '../src/proto/grpc/testing/messages_pb'
require_relative '../src/proto/grpc/testing/test_services_pb'

AUTH_ENV = Google::Auth::CredentialsLoader::ENV_VAR

# RubyLogger defines a logger for gRPC based on the standard ruby logger.
module RubyLogger
  def logger
    LOGGER
  end

  LOGGER = Logger.new(STDOUT)
  LOGGER.level = Logger::INFO
end

# GRPC is the general RPC module
module GRPC
  # Inject the noop #logger if no module-level logger method has been injected.
  extend RubyLogger
end

# AssertionError is use to indicate interop test failures.
class AssertionError < RuntimeError; end

# Fails with AssertionError if the block does evaluate to true
def assert(msg = 'unknown cause')
  fail 'No assertion block provided' unless block_given?
  fail AssertionError, msg unless yield
end

# loads the certificates used to access the test server securely.
def load_test_certs
  this_dir = File.expand_path(File.dirname(__FILE__))
  data_dir = File.join(File.dirname(File.dirname(this_dir)), 'spec/testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(data_dir, f)).read }
end

# creates SSL Credentials from the test certificates.
def test_creds
  certs = load_test_certs
  GRPC::Core::ChannelCredentials.new(certs[0])
end

# creates SSL Credentials from the production certificates.
def prod_creds
  GRPC::Core::ChannelCredentials.new()
end

# creates the SSL Credentials.
def ssl_creds(use_test_ca)
  return test_creds if use_test_ca
  prod_creds
end

# creates a test stub that accesses host:port securely.
def create_stub(opts)
  address = "#{opts.server_host}:#{opts.server_port}"

  # Provide channel args that request compression by default
  # for compression interop tests
  if ['client_compressed_unary',
      'client_compressed_streaming'].include?(opts.test_case)
    compression_options =
      GRPC::Core::CompressionOptions.new(default_algorithm: :gzip)
    compression_channel_args = compression_options.to_channel_arg_hash
  else
    compression_channel_args = {}
  end

  if opts.secure
    creds = ssl_creds(opts.use_test_ca)
    stub_opts = {
      channel_args: {}
    }
    unless opts.server_host_override.empty?
      stub_opts[:channel_args].merge!({
          GRPC::Core::Channel::SSL_TARGET => opts.server_host_override
      })
    end

    # Add service account creds if specified
    wants_creds = %w(all compute_engine_creds service_account_creds)
    if wants_creds.include?(opts.test_case)
      unless opts.oauth_scope.nil?
        auth_creds = Google::Auth.get_application_default(opts.oauth_scope)
        call_creds = GRPC::Core::CallCredentials.new(auth_creds.updater_proc)
        creds = creds.compose call_creds
      end
    end

    if opts.test_case == 'oauth2_auth_token'
      auth_creds = Google::Auth.get_application_default(opts.oauth_scope)
      kw = auth_creds.updater_proc.call({})  # gives as an auth token

      # use a metadata update proc that just adds the auth token.
      call_creds = GRPC::Core::CallCredentials.new(proc { |md| md.merge(kw) })
      creds = creds.compose call_creds
    end

    if opts.test_case == 'jwt_token_creds'  # don't use a scope
      auth_creds = Google::Auth.get_application_default
      call_creds = GRPC::Core::CallCredentials.new(auth_creds.updater_proc)
      creds = creds.compose call_creds
    end

    GRPC.logger.info("... connecting securely to #{address}")
    stub_opts[:channel_args].merge!(compression_channel_args)
    if opts.test_case == "unimplemented_service"
      Grpc::Testing::UnimplementedService::Stub.new(address, creds, **stub_opts)
    else
      Grpc::Testing::TestService::Stub.new(address, creds, **stub_opts)
    end
  else
    GRPC.logger.info("... connecting insecurely to #{address}")
    if opts.test_case == "unimplemented_service"
      Grpc::Testing::UnimplementedService::Stub.new(
        address,
        :this_channel_is_insecure,
        channel_args: compression_channel_args
      )
    else
      Grpc::Testing::TestService::Stub.new(
        address,
        :this_channel_is_insecure,
        channel_args: compression_channel_args
      )
    end
  end
end

# produces a string of null chars (\0) of length l.
def nulls(l)
  fail 'requires #{l} to be +ve' if l < 0
  [].pack('x' * l).force_encoding('ascii-8bit')
end

# a PingPongPlayer implements the ping pong bidi test.
class PingPongPlayer
  include Grpc::Testing
  include Grpc::Testing::PayloadType
  attr_accessor :queue
  attr_accessor :canceller_op

  # reqs is the enumerator over the requests
  def initialize(msg_sizes)
    @queue = Queue.new
    @msg_sizes = msg_sizes
    @canceller_op = nil  # used to cancel after the first response
  end

  def each_item
    return enum_for(:each_item) unless block_given?
    req_cls, p_cls = StreamingOutputCallRequest, ResponseParameters  # short
    count = 0
    @msg_sizes.each do |m|
      req_size, resp_size = m
      req = req_cls.new(payload: Payload.new(body: nulls(req_size)),
                        response_type: :COMPRESSABLE,
                        response_parameters: [p_cls.new(size: resp_size)])
      yield req
      resp = @queue.pop
      assert('payload type is wrong') { :COMPRESSABLE == resp.payload.type }
      assert("payload body #{count} has the wrong length") do
        resp_size == resp.payload.body.length
      end
      p "OK: ping_pong #{count}"
      count += 1
      unless @canceller_op.nil?
        canceller_op.cancel
        break
      end
    end
  end
end

class BlockingEnumerator
  include Grpc::Testing
  include Grpc::Testing::PayloadType

  def initialize(req_size, sleep_time)
    @req_size = req_size
    @sleep_time = sleep_time
  end

  def each_item
    return enum_for(:each_item) unless block_given?
    req_cls = StreamingOutputCallRequest
    req = req_cls.new(payload: Payload.new(body: nulls(@req_size)))
    yield req
    # Sleep until after the deadline should have passed
    sleep(@sleep_time)
  end
end

# Intended to be used to wrap a call_op, and to adjust
# the write flag of the call_op in between messages yielded to it.
class WriteFlagSettingStreamingInputEnumerable
  attr_accessor :call_op

  def initialize(requests_and_write_flags)
    @requests_and_write_flags = requests_and_write_flags
  end

  def each
    @requests_and_write_flags.each do |request_and_flag|
      @call_op.write_flag = request_and_flag[:write_flag]
      yield request_and_flag[:request]
    end
  end
end

# defines methods corresponding to each interop test case.
class NamedTests
  include Grpc::Testing
  include Grpc::Testing::PayloadType
  include GRPC::Core::MetadataKeys

  def initialize(stub, args)
    @stub = stub
    @args = args
  end

  def empty_unary
    resp = @stub.empty_call(Empty.new)
    assert('empty_unary: invalid response') { resp.is_a?(Empty) }
  end

  def large_unary
    perform_large_unary
  end

  def client_compressed_unary
    # first request used also for the probe
    req_size, wanted_response_size = 271_828, 314_159
    expect_compressed = BoolValue.new(value: true)
    payload = Payload.new(type: :COMPRESSABLE, body: nulls(req_size))
    req = SimpleRequest.new(response_type: :COMPRESSABLE,
                            response_size: wanted_response_size,
                            payload: payload,
                            expect_compressed: expect_compressed)

    # send a probe to see if CompressedResponse is supported on the server
    send_probe_for_compressed_request_support do
      request_uncompressed_args = {
        COMPRESSION_REQUEST_ALGORITHM => 'identity'
      }
      @stub.unary_call(req, metadata: request_uncompressed_args)
    end

    # make a call with a compressed message
    resp = @stub.unary_call(req)
    assert('Expected second unary call with compression to work') do
      resp.payload.body.length == wanted_response_size
    end

    # make a call with an uncompressed message
    stub_options = {
      COMPRESSION_REQUEST_ALGORITHM => 'identity'
    }

    req = SimpleRequest.new(
      response_type: :COMPRESSABLE,
      response_size: wanted_response_size,
      payload: payload,
      expect_compressed: BoolValue.new(value: false)
    )

    resp = @stub.unary_call(req, metadata: stub_options)
    assert('Expected second unary call with compression to work') do
      resp.payload.body.length == wanted_response_size
    end
  end

  def service_account_creds
    # ignore this test if the oauth options are not set
    if @args.oauth_scope.nil?
      p 'NOT RUN: service_account_creds; no service_account settings'
      return
    end
    json_key = File.read(ENV[AUTH_ENV])
    wanted_email = MultiJson.load(json_key)['client_email']
    resp = perform_large_unary(fill_username: true,
                               fill_oauth_scope: true)
    assert("#{__callee__}: bad username") { wanted_email == resp.username }
    assert("#{__callee__}: bad oauth scope") do
      @args.oauth_scope.include?(resp.oauth_scope)
    end
  end

  def jwt_token_creds
    json_key = File.read(ENV[AUTH_ENV])
    wanted_email = MultiJson.load(json_key)['client_email']
    resp = perform_large_unary(fill_username: true)
    assert("#{__callee__}: bad username") { wanted_email == resp.username }
  end

  def compute_engine_creds
    resp = perform_large_unary(fill_username: true,
                               fill_oauth_scope: true)
    assert("#{__callee__}: bad username") do
      @args.default_service_account == resp.username
    end
  end

  def oauth2_auth_token
    resp = perform_large_unary(fill_username: true,
                               fill_oauth_scope: true)
    json_key = File.read(ENV[AUTH_ENV])
    wanted_email = MultiJson.load(json_key)['client_email']
    assert("#{__callee__}: bad username") { wanted_email == resp.username }
    assert("#{__callee__}: bad oauth scope") do
      @args.oauth_scope.include?(resp.oauth_scope)
    end
  end

  def per_rpc_creds
    auth_creds = Google::Auth.get_application_default(@args.oauth_scope)
    update_metadata = proc do |md|
      kw = auth_creds.updater_proc.call({})
    end

    call_creds = GRPC::Core::CallCredentials.new(update_metadata)

    resp = perform_large_unary(fill_username: true,
                               fill_oauth_scope: true,
                               credentials: call_creds)
    json_key = File.read(ENV[AUTH_ENV])
    wanted_email = MultiJson.load(json_key)['client_email']
    assert("#{__callee__}: bad username") { wanted_email == resp.username }
    assert("#{__callee__}: bad oauth scope") do
      @args.oauth_scope.include?(resp.oauth_scope)
    end
  end

  def client_streaming
    msg_sizes = [27_182, 8, 1828, 45_904]
    wanted_aggregate_size = 74_922
    reqs = msg_sizes.map do |x|
      req = Payload.new(body: nulls(x))
      StreamingInputCallRequest.new(payload: req)
    end
    resp = @stub.streaming_input_call(reqs)
    assert("#{__callee__}: aggregate payload size is incorrect") do
      wanted_aggregate_size == resp.aggregated_payload_size
    end
  end

  def client_compressed_streaming
    # first request used also by the probe
    first_request = StreamingInputCallRequest.new(
      payload: Payload.new(type: :COMPRESSABLE, body: nulls(27_182)),
      expect_compressed: BoolValue.new(value: true)
    )

    # send a probe to see if CompressedResponse is supported on the server
    send_probe_for_compressed_request_support do
      request_uncompressed_args = {
        COMPRESSION_REQUEST_ALGORITHM => 'identity'
      }
      @stub.streaming_input_call([first_request],
                                 metadata: request_uncompressed_args)
    end

    second_request = StreamingInputCallRequest.new(
      payload: Payload.new(type: :COMPRESSABLE, body: nulls(45_904)),
      expect_compressed: BoolValue.new(value: false)
    )

    # Create the requests messages and the corresponding write flags
    # for each message
    requests = WriteFlagSettingStreamingInputEnumerable.new([
      { request: first_request,
        write_flag: 0 },
      { request: second_request,
        write_flag: GRPC::Core::WriteFlags::NO_COMPRESS }
    ])

    # Create the call_op, pass it to the requests enumerable, and
    # run the call
    call_op = @stub.streaming_input_call(requests,
                                         return_op: true)
    requests.call_op = call_op
    resp = call_op.execute

    wanted_aggregate_size = 73_086

    assert("#{__callee__}: aggregate payload size is incorrect") do
      wanted_aggregate_size == resp.aggregated_payload_size
    end
  end

  def server_streaming
    msg_sizes = [31_415, 9, 2653, 58_979]
    response_spec = msg_sizes.map { |s| ResponseParameters.new(size: s) }
    req = StreamingOutputCallRequest.new(response_type: :COMPRESSABLE,
                                         response_parameters: response_spec)
    resps = @stub.streaming_output_call(req)
    resps.each_with_index do |r, i|
      assert("#{__callee__}: too many responses") { i < msg_sizes.length }
      assert("#{__callee__}: payload body #{i} has the wrong length") do
        msg_sizes[i] == r.payload.body.length
      end
      assert("#{__callee__}: payload type is wrong") do
        :COMPRESSABLE == r.payload.type
      end
    end
  end

  def ping_pong
    msg_sizes = [[27_182, 31_415], [8, 9], [1828, 2653], [45_904, 58_979]]
    ppp = PingPongPlayer.new(msg_sizes)
    resps = @stub.full_duplex_call(ppp.each_item)
    resps.each { |r| ppp.queue.push(r) }
  end

  def timeout_on_sleeping_server
    enum = BlockingEnumerator.new(27_182, 2)
    deadline = GRPC::Core::TimeConsts::from_relative_time(1)
    resps = @stub.full_duplex_call(enum.each_item, deadline: deadline)
    resps.each { } # wait to receive each request (or timeout)
    fail 'Should have raised GRPC::DeadlineExceeded'
  rescue GRPC::DeadlineExceeded
  end

  def empty_stream
    ppp = PingPongPlayer.new([])
    resps = @stub.full_duplex_call(ppp.each_item)
    count = 0
    resps.each do |r|
      ppp.queue.push(r)
      count += 1
    end
    assert("#{__callee__}: too many responses expected 0") do
      count == 0
    end
  end

  def cancel_after_begin
    msg_sizes = [27_182, 8, 1828, 45_904]
    reqs = msg_sizes.map do |x|
      req = Payload.new(body: nulls(x))
      StreamingInputCallRequest.new(payload: req)
    end
    op = @stub.streaming_input_call(reqs, return_op: true)
    op.cancel
    op.execute
    fail 'Should have raised GRPC:Cancelled'
  rescue GRPC::Cancelled
    assert("#{__callee__}: call operation should be CANCELLED") { op.cancelled? }
  end

  def cancel_after_first_response
    msg_sizes = [[27_182, 31_415], [8, 9], [1828, 2653], [45_904, 58_979]]
    ppp = PingPongPlayer.new(msg_sizes)
    op = @stub.full_duplex_call(ppp.each_item, return_op: true)
    ppp.canceller_op = op  # causes ppp to cancel after the 1st message
    op.execute.each { |r| ppp.queue.push(r) }
    fail 'Should have raised GRPC:Cancelled'
  rescue GRPC::Cancelled
    assert("#{__callee__}: call operation should be CANCELLED") { op.cancelled? }
    op.wait
  end

  def unimplemented_method
    begin
      resp = @stub.unimplemented_call(Empty.new)
    rescue GRPC::Unimplemented => e
      return
    rescue Exception => e
      fail AssertionError, "Expected BadStatus. Received: #{e.inspect}"
    end
    fail AssertionError, "GRPC::Unimplemented should have been raised. Was not."
  end

  def unimplemented_service
    begin
      resp = @stub.unimplemented_call(Empty.new)
    rescue GRPC::Unimplemented => e
      return
    rescue Exception => e
      fail AssertionError, "Expected BadStatus. Received: #{e.inspect}"
    end
    fail AssertionError, "GRPC::Unimplemented should have been raised. Was not."
  end

  def status_code_and_message

    # Function wide constants.
    message = "test status method"
    code = GRPC::Core::StatusCodes::UNKNOWN

    # Testing with UnaryCall.
    payload = Payload.new(type: :COMPRESSABLE, body: nulls(1))
    echo_status = EchoStatus.new(code: code, message: message)
    req = SimpleRequest.new(response_type: :COMPRESSABLE,
			    response_size: 1,
			    payload: payload,
			    response_status: echo_status)
    seen_correct_exception = false
    begin
      resp = @stub.unary_call(req)
    rescue GRPC::Unknown => e
      if e.details != message
	      fail AssertionError,
	        "Expected message #{message}. Received: #{e.details}"
      end
      seen_correct_exception = true
    rescue Exception => e
      fail AssertionError, "Expected BadStatus. Received: #{e.inspect}"
    end

    if not seen_correct_exception
      fail AssertionError, "Did not see expected status from UnaryCall"
    end

    # testing with FullDuplex
    req_cls, p_cls = StreamingOutputCallRequest, ResponseParameters
    duplex_req = req_cls.new(payload: Payload.new(body: nulls(1)),
                  response_type: :COMPRESSABLE,
                  response_parameters: [p_cls.new(size: 1)],
                  response_status: echo_status)
    seen_correct_exception = false
    begin
      resp = @stub.full_duplex_call([duplex_req])
      resp.each { |r| }
    rescue GRPC::Unknown => e
      if e.details != message
        fail AssertionError,
          "Expected message #{message}. Received: #{e.details}"
      end
      seen_correct_exception = true
    rescue Exception => e
      fail AssertionError, "Expected BadStatus. Received: #{e.inspect}"
    end

    if not seen_correct_exception
      fail AssertionError, "Did not see expected status from FullDuplexCall"
    end

  end


  def custom_metadata

    # Function wide constants
    req_size, wanted_response_size = 271_828, 314_159
    initial_metadata_key = "x-grpc-test-echo-initial"
    initial_metadata_value = "test_initial_metadata_value"
    trailing_metadata_key = "x-grpc-test-echo-trailing-bin"
    trailing_metadata_value = "\x0a\x0b\x0a\x0b\x0a\x0b"

    metadata = {
      initial_metadata_key => initial_metadata_value,
      trailing_metadata_key => trailing_metadata_value
    }

    # Testing with UnaryCall
    payload = Payload.new(type: :COMPRESSABLE, body: nulls(req_size))
    req = SimpleRequest.new(response_type: :COMPRESSABLE,
			    response_size: wanted_response_size,
			    payload: payload)

    op = @stub.unary_call(req, metadata: metadata, return_op: true)
    op.execute
    if not op.metadata.has_key?(initial_metadata_key)
      fail AssertionError, "Expected initial metadata. None received"
    elsif op.metadata[initial_metadata_key] != metadata[initial_metadata_key]
      fail AssertionError,
             "Expected initial metadata: #{metadata[initial_metadata_key]}. "\
             "Received: #{op.metadata[initial_metadata_key]}"
    end
    if not op.trailing_metadata.has_key?(trailing_metadata_key)
      fail AssertionError, "Expected trailing metadata. None received"
    elsif op.trailing_metadata[trailing_metadata_key] !=
          metadata[trailing_metadata_key]
      fail AssertionError,
            "Expected trailing metadata: #{metadata[trailing_metadata_key]}. "\
            "Received: #{op.trailing_metadata[trailing_metadata_key]}"
    end

    # Testing with FullDuplex
    req_cls, p_cls = StreamingOutputCallRequest, ResponseParameters
    duplex_req = req_cls.new(payload: Payload.new(body: nulls(req_size)),
                  response_type: :COMPRESSABLE,
                  response_parameters: [p_cls.new(size: wanted_response_size)])

    duplex_op = @stub.full_duplex_call([duplex_req], metadata: metadata,
                                        return_op: true)
    resp = duplex_op.execute
    resp.each { |r| } # ensures that the server sends trailing data
    duplex_op.wait
    if not duplex_op.metadata.has_key?(initial_metadata_key)
      fail AssertionError, "Expected initial metadata. None received"
    elsif duplex_op.metadata[initial_metadata_key] !=
          metadata[initial_metadata_key]
      fail AssertionError,
             "Expected initial metadata: #{metadata[initial_metadata_key]}. "\
             "Received: #{duplex_op.metadata[initial_metadata_key]}"
    end
    if not duplex_op.trailing_metadata[trailing_metadata_key]
      fail AssertionError, "Expected trailing metadata. None received"
    elsif duplex_op.trailing_metadata[trailing_metadata_key] !=
          metadata[trailing_metadata_key]
      fail AssertionError,
          "Expected trailing metadata: #{metadata[trailing_metadata_key]}. "\
          "Received: #{duplex_op.trailing_metadata[trailing_metadata_key]}"
    end

  end

  def all
    all_methods = NamedTests.instance_methods(false).map(&:to_s)
    all_methods.each do |m|
      next if m == 'all' || m.start_with?('assert')
      p "TESTCASE: #{m}"
      method(m).call
    end
  end

  private

  def perform_large_unary(fill_username: false, fill_oauth_scope: false, **kw)
    req_size, wanted_response_size = 271_828, 314_159
    payload = Payload.new(type: :COMPRESSABLE, body: nulls(req_size))
    req = SimpleRequest.new(response_type: :COMPRESSABLE,
                            response_size: wanted_response_size,
                            payload: payload)
    req.fill_username = fill_username
    req.fill_oauth_scope = fill_oauth_scope
    resp = @stub.unary_call(req, **kw)
    assert('payload type is wrong') do
      :COMPRESSABLE == resp.payload.type
    end
    assert('payload body has the wrong length') do
      wanted_response_size == resp.payload.body.length
    end
    assert('payload body is invalid') do
      nulls(wanted_response_size) == resp.payload.body
    end
    resp
  end

  # Send probing message for compressed request on the server, to see
  # if it's implemented.
  def send_probe_for_compressed_request_support(&send_probe)
    bad_status_occurred = false

    begin
      send_probe.call
    rescue GRPC::BadStatus => e
      if e.code == GRPC::Core::StatusCodes::INVALID_ARGUMENT
        bad_status_occurred = true
      else
        fail AssertionError, "Bad status received but code is #{e.code}"
      end
    rescue Exception => e
      fail AssertionError, "Expected BadStatus. Received: #{e.inspect}"
    end

    assert('CompressedRequest probe failed') do
      bad_status_occurred
    end
  end

end

# Args is used to hold the command line info.
Args = Struct.new(:default_service_account, :server_host, :server_host_override,
                  :oauth_scope, :server_port, :secure, :test_case,
                  :use_test_ca)

# validates the command line options, returning them as a Hash.
def parse_args
  args = Args.new
  args.server_host_override = ''
  OptionParser.new do |opts|
    opts.on('--oauth_scope scope',
            'Scope for OAuth tokens') { |v| args['oauth_scope'] = v }
    opts.on('--server_host SERVER_HOST', 'server hostname') do |v|
      args['server_host'] = v
    end
    opts.on('--default_service_account email_address',
            'email address of the default service account') do |v|
      args['default_service_account'] = v
    end
    opts.on('--server_host_override HOST_OVERRIDE',
            'override host via a HTTP header') do |v|
      args['server_host_override'] = v
    end
    opts.on('--server_port SERVER_PORT', 'server port') do |v|
      args['server_port'] = v
    end
    # instance_methods(false) gives only the methods defined in that class
    test_cases = NamedTests.instance_methods(false).map(&:to_s)
    test_case_list = test_cases.join(',')
    opts.on('--test_case CODE', test_cases, {}, 'select a test_case',
            "  (#{test_case_list})") { |v| args['test_case'] = v }
    opts.on('--use_tls USE_TLS', ['false', 'true'],
            'require a secure connection?') do |v|
      args['secure'] = v == 'true'
    end
    opts.on('--use_test_ca USE_TEST_CA', ['false', 'true'],
            'if secure, use the test certificate?') do |v|
      args['use_test_ca'] = v == 'true'
    end
  end.parse!
  _check_args(args)
end

def _check_args(args)
  %w(server_host server_port test_case).each do |a|
    if args[a].nil?
      fail(OptionParser::MissingArgument, "please specify --#{a}")
    end
  end
  args
end

def main
  opts = parse_args
  stub = create_stub(opts)
  NamedTests.new(stub, opts).method(opts['test_case']).call
  p "OK: #{opts['test_case']}"
end

if __FILE__ == $0
  main
end
