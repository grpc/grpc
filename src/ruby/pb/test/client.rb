#!/usr/bin/env ruby

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

# client is a testing tool that accesses a gRPC interop testing server and runs
# a test on it.
#
# Helps validate interoperation b/w different gRPC implementations.
#
# Usage: $ path/to/client.rb --server_host=<hostname> \
#                            --server_port=<port> \
#                            --test_case=<testcase_name>

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(File.dirname(this_dir)), 'lib')
pb_dir = File.dirname(File.dirname(this_dir))
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)
$LOAD_PATH.unshift(pb_dir) unless $LOAD_PATH.include?(pb_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'optparse'
require 'logger'

require 'grpc'
require 'googleauth'
require 'google/protobuf'

require 'test/proto/empty'
require 'test/proto/messages'
require 'test/proto/test_services'

require 'signet/ssl_config'

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

# loads the certificates used to access the test server securely.
def load_prod_cert
  fail 'could not find a production cert' if ENV['SSL_CERT_FILE'].nil?
  GRPC.logger.info("loading prod certs from #{ENV['SSL_CERT_FILE']}")
  File.open(ENV['SSL_CERT_FILE']).read
end

# creates SSL Credentials from the test certificates.
def test_creds
  certs = load_test_certs
  GRPC::Core::Credentials.new(certs[0])
end

# creates SSL Credentials from the production certificates.
def prod_creds
  cert_text = load_prod_cert
  GRPC::Core::Credentials.new(cert_text)
end

# creates the SSL Credentials.
def ssl_creds(use_test_ca)
  return test_creds if use_test_ca
  prod_creds
end

# creates a test stub that accesses host:port securely.
def create_stub(opts)
  address = "#{opts.host}:#{opts.port}"
  if opts.secure
    stub_opts = {
      :creds => ssl_creds(opts.use_test_ca),
      GRPC::Core::Channel::SSL_TARGET => opts.host_override
    }

    # Add service account creds if specified
    wants_creds = %w(all compute_engine_creds service_account_creds)
    if wants_creds.include?(opts.test_case)
      unless opts.oauth_scope.nil?
        auth_creds = Google::Auth.get_application_default(opts.oauth_scope)
        stub_opts[:update_metadata] = auth_creds.updater_proc
      end
    end

    if opts.test_case == 'oauth2_auth_token'
      auth_creds = Google::Auth.get_application_default(opts.oauth_scope)
      kw = auth_creds.updater_proc.call({})  # gives as an auth token

      # use a metadata update proc that just adds the auth token.
      stub_opts[:update_metadata] = proc { |md| md.merge(kw) }
    end

    if opts.test_case == 'jwt_token_creds'  # don't use a scope
      auth_creds = Google::Auth.get_application_default
      stub_opts[:update_metadata] = auth_creds.updater_proc
    end

    GRPC.logger.info("... connecting securely to #{address}")
    Grpc::Testing::TestService::Stub.new(address, **stub_opts)
  else
    GRPC.logger.info("... connecting insecurely to #{address}")
    Grpc::Testing::TestService::Stub.new(address)
  end
end

# produces a string of null chars (\0) of length l.
def nulls(l)
  fail 'requires #{l} to be +ve' if l < 0
  [].pack('x' * l).force_encoding('utf-8')
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

# defines methods corresponding to each interop test case.
class NamedTests
  include Grpc::Testing
  include Grpc::Testing::PayloadType

  def initialize(stub, args)
    @stub = stub
    @args = args
  end

  def empty_unary
    resp = @stub.empty_call(Empty.new)
    assert('empty_unary: invalid response') { resp.is_a?(Empty) }
    p 'OK: empty_unary'
  end

  def large_unary
    perform_large_unary
    p 'OK: large_unary'
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
    p "OK: #{__callee__}"
  end

  def jwt_token_creds
    json_key = File.read(ENV[AUTH_ENV])
    wanted_email = MultiJson.load(json_key)['client_email']
    resp = perform_large_unary(fill_username: true)
    assert("#{__callee__}: bad username") { wanted_email == resp.username }
    p "OK: #{__callee__}"
  end

  def compute_engine_creds
    resp = perform_large_unary(fill_username: true,
                               fill_oauth_scope: true)
    assert("#{__callee__}: bad username") do
      @args.default_service_account == resp.username
    end
    p "OK: #{__callee__}"
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
    p "OK: #{__callee__}"
  end

  def per_rpc_creds
    auth_creds = Google::Auth.get_application_default(@args.oauth_scope)
    kw = auth_creds.updater_proc.call({})

    # TODO(jtattermusch): downcase the metadata keys here to make sure
    # they are not rejected by C core. This is a hotfix that should
    # be addressed by introducing auto-downcasing logic.
    kw = Hash[ kw.each_pair.map { |k, v|  [k.downcase, v] }]

    resp = perform_large_unary(fill_username: true,
                               fill_oauth_scope: true,
                               **kw)
    json_key = File.read(ENV[AUTH_ENV])
    wanted_email = MultiJson.load(json_key)['client_email']
    assert("#{__callee__}: bad username") { wanted_email == resp.username }
    assert("#{__callee__}: bad oauth scope") do
      @args.oauth_scope.include?(resp.oauth_scope)
    end
    p "OK: #{__callee__}"
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
    p "OK: #{__callee__}"
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
    p "OK: #{__callee__}"
  end

  def ping_pong
    msg_sizes = [[27_182, 31_415], [8, 9], [1828, 2653], [45_904, 58_979]]
    ppp = PingPongPlayer.new(msg_sizes)
    resps = @stub.full_duplex_call(ppp.each_item)
    resps.each { |r| ppp.queue.push(r) }
    p "OK: #{__callee__}"
  end

  def timeout_on_sleeping_server
    msg_sizes = [[27_182, 31_415]]
    ppp = PingPongPlayer.new(msg_sizes)
    resps = @stub.full_duplex_call(ppp.each_item, timeout: 0.001)
    resps.each { |r| ppp.queue.push(r) }
    fail 'Should have raised GRPC::BadStatus(DEADLINE_EXCEEDED)'
  rescue GRPC::BadStatus => e
    assert("#{__callee__}: status was wrong") do
      e.code == GRPC::Core::StatusCodes::DEADLINE_EXCEEDED
    end
    p "OK: #{__callee__}"
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
    p "OK: #{__callee__}"
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
    assert("#{__callee__}: call operation should be CANCELLED") { op.cancelled }
    p "OK: #{__callee__}"
  end

  def cancel_after_first_response
    msg_sizes = [[27_182, 31_415], [8, 9], [1828, 2653], [45_904, 58_979]]
    ppp = PingPongPlayer.new(msg_sizes)
    op = @stub.full_duplex_call(ppp.each_item, return_op: true)
    ppp.canceller_op = op  # causes ppp to cancel after the 1st message
    op.execute.each { |r| ppp.queue.push(r) }
    fail 'Should have raised GRPC:Cancelled'
  rescue GRPC::Cancelled
    assert("#{__callee__}: call operation should be CANCELLED") { op.cancelled }
    op.wait
    p "OK: #{__callee__}"
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
end

# Args is used to hold the command line info.
Args = Struct.new(:default_service_account, :host, :host_override,
                  :oauth_scope, :port, :secure, :test_case,
                  :use_test_ca)

# validates the the command line options, returning them as a Hash.
def parse_args
  args = Args.new
  args.host_override = 'foo.test.google.fr'
  OptionParser.new do |opts|
    opts.on('--oauth_scope scope',
            'Scope for OAuth tokens') { |v| args['oauth_scope'] = v }
    opts.on('--server_host SERVER_HOST', 'server hostname') do |v|
      args['host'] = v
    end
    opts.on('--default_service_account email_address',
            'email address of the default service account') do |v|
      args['default_service_account'] = v
    end
    opts.on('--server_host_override HOST_OVERRIDE',
            'override host via a HTTP header') do |v|
      args['host_override'] = v
    end
    opts.on('--server_port SERVER_PORT', 'server port') { |v| args['port'] = v }
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
  %w(host port test_case).each do |a|
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
end

main
