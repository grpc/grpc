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

# interop_client is a testing tool that accesses a gRPC interop testing
# server and runs a test on it.
#
# Helps validate interoperation b/w different gRPC implementations.
#
# Usage: $ path/to/interop_client.rb --server_host=<hostname> \
#                                    --server_port=<port> \
#                                    --test_case=<testcase_name>

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(File.dirname(this_dir)), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'optparse'
require 'minitest'
require 'minitest/assertions'

require 'grpc'
require 'googleauth'
require 'google/protobuf'

require 'test/cpp/interop/test_services'
require 'test/cpp/interop/messages'
require 'test/cpp/interop/empty'

require 'signet/ssl_config'

AUTH_ENV = Google::Auth::CredentialsLoader::ENV_VAR

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
  include Minitest::Assertions
  include Grpc::Testing
  include Grpc::Testing::PayloadType
  attr_accessor :assertions # required by Minitest::Assertions
  attr_accessor :queue
  attr_accessor :canceller_op

  # reqs is the enumerator over the requests
  def initialize(msg_sizes)
    @queue = Queue.new
    @msg_sizes = msg_sizes
    @assertions = 0  # required by Minitest::Assertions
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
      assert_equal(:COMPRESSABLE, resp.payload.type, 'payload type is wrong')
      assert_equal(resp_size, resp.payload.body.length,
                   "payload body #{count} has the wrong length")
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
  include Minitest::Assertions
  include Grpc::Testing
  include Grpc::Testing::PayloadType
  attr_accessor :assertions # required by Minitest::Assertions

  def initialize(stub, args)
    @assertions = 0  # required by Minitest::Assertions
    @stub = stub
    @args = args
  end

  def empty_unary
    resp = @stub.empty_call(Empty.new)
    assert resp.is_a?(Empty), 'empty_unary: invalid response'
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
    assert_equal(wanted_email, resp.username,
                 'service_account_creds: incorrect username')
    assert(@args.oauth_scope.include?(resp.oauth_scope),
           'service_account_creds: incorrect oauth_scope')
    p 'OK: service_account_creds'
  end

  def jwt_token_creds
    json_key = File.read(ENV[AUTH_ENV])
    wanted_email = MultiJson.load(json_key)['client_email']
    resp = perform_large_unary(fill_username: true)
    assert_equal(wanted_email, resp.username,
                 'service_account_creds: incorrect username')
    p 'OK: jwt_token_creds'
  end

  def compute_engine_creds
    resp = perform_large_unary(fill_username: true,
                               fill_oauth_scope: true)
    assert_equal(@args.default_service_account, resp.username,
                 'compute_engine_creds: incorrect username')
    p 'OK: compute_engine_creds'
  end

  def client_streaming
    msg_sizes = [27_182, 8, 1828, 45_904]
    wanted_aggregate_size = 74_922
    reqs = msg_sizes.map do |x|
      req = Payload.new(body: nulls(x))
      StreamingInputCallRequest.new(payload: req)
    end
    resp = @stub.streaming_input_call(reqs)
    assert_equal(wanted_aggregate_size, resp.aggregated_payload_size,
                 'client_streaming: aggregate payload size is incorrect')
    p 'OK: client_streaming'
  end

  def server_streaming
    msg_sizes = [31_415, 9, 2653, 58_979]
    response_spec = msg_sizes.map { |s| ResponseParameters.new(size: s) }
    req = StreamingOutputCallRequest.new(response_type: :COMPRESSABLE,
                                         response_parameters: response_spec)
    resps = @stub.streaming_output_call(req)
    resps.each_with_index do |r, i|
      assert i < msg_sizes.length, 'too many responses'
      assert_equal(:COMPRESSABLE, r.payload.type,
                   'payload type is wrong')
      assert_equal(msg_sizes[i], r.payload.body.length,
                   'payload body #{i} has the wrong length')
    end
    p 'OK: server_streaming'
  end

  def ping_pong
    msg_sizes = [[27_182, 31_415], [8, 9], [1828, 2653], [45_904, 58_979]]
    ppp = PingPongPlayer.new(msg_sizes)
    resps = @stub.full_duplex_call(ppp.each_item)
    resps.each { |r| ppp.queue.push(r) }
    p 'OK: ping_pong'
  end

  def cancel_after_begin
    msg_sizes = [27_182, 8, 1828, 45_904]
    reqs = msg_sizes.map do |x|
      req = Payload.new(body: nulls(x))
      StreamingInputCallRequest.new(payload: req)
    end
    op = @stub.streaming_input_call(reqs, return_op: true)
    op.cancel
    assert_raises(GRPC::Cancelled) { op.execute }
    p 'OK: cancel_after_begin'
  end

  def cancel_after_first_response
    msg_sizes = [[27_182, 31_415], [8, 9], [1828, 2653], [45_904, 58_979]]
    ppp = PingPongPlayer.new(msg_sizes)
    op = @stub.full_duplex_call(ppp.each_item, return_op: true)
    ppp.canceller_op = op  # causes ppp to cancel after the 1st message
    assert_raises(GRPC::Cancelled) { op.execute.each { |r| ppp.queue.push(r) } }
    p 'OK: cancel_after_first_response'
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

  def perform_large_unary(fill_username: false, fill_oauth_scope: false)
    req_size, wanted_response_size = 271_828, 314_159
    payload = Payload.new(type: :COMPRESSABLE, body: nulls(req_size))
    req = SimpleRequest.new(response_type: :COMPRESSABLE,
                            response_size: wanted_response_size,
                            payload: payload)
    req.fill_username = fill_username
    req.fill_oauth_scope = fill_oauth_scope
    resp = @stub.unary_call(req)
    assert_equal(:COMPRESSABLE, resp.payload.type,
                 'large_unary: payload had the wrong type')
    assert_equal(wanted_response_size, resp.payload.body.length,
                 'large_unary: payload had the wrong length')
    assert_equal(nulls(wanted_response_size), resp.payload.body,
                 'large_unary: payload content is invalid')
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
    opts.on('-s', '--use_tls', 'require a secure connection?') do |v|
      args['secure'] = v
    end
    opts.on('-t', '--use_test_ca',
            'if secure, use the test certificate?') do |v|
      args['use_test_ca'] = v
    end
  end.parse!
  _check_args(args)
end

def _check_args(args)
  %w(host port test_case).each do |a|
    if args[a].nil?
      fail(OptionParser::MissingArgument, "please specify --#{arg}")
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
