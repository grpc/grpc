#!/usr/bin/env ruby
#
# Copyright 2016 gRPC authors.
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

this_dir = File.expand_path(File.dirname(__FILE__))
protos_lib_dir = File.join(this_dir, 'lib')
grpc_lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(grpc_lib_dir) unless $LOAD_PATH.include?(grpc_lib_dir)
$LOAD_PATH.unshift(protos_lib_dir) unless $LOAD_PATH.include?(protos_lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'grpc'
require 'end2end_common'

def create_channel_creds
  test_root = File.join(File.dirname(__FILE__), '..', 'spec', 'testdata')
  files = ['ca.pem', 'client.key', 'client.pem']
  creds = files.map { |f| File.open(File.join(test_root, f)).read }
  GRPC::Core::ChannelCredentials.new(creds[0], creds[1], creds[2])
end

def client_cert
  test_root = File.join(File.dirname(__FILE__), '..', 'spec', 'testdata')
  cert = File.open(File.join(test_root, 'client.pem')).read
  fail unless cert.is_a?(String)
  cert
end

def create_server_creds
  test_root = File.join(File.dirname(__FILE__), '..', 'spec', 'testdata')
  GRPC.logger.info("test root: #{test_root}")
  files = ['ca.pem', 'server1.key', 'server1.pem']
  creds = files.map { |f| File.open(File.join(test_root, f)).read }
  GRPC::Core::ServerCredentials.new(
    creds[0],
    [{ private_key: creds[1], cert_chain: creds[2] }],
    true) # force client auth
end

def check_rpcs_still_possible(stub)
  # Expect three more RPCs to succeed. Use a retry loop because the server
  # thread pool might still be busy processing the previous round of RPCs.
  3.times do
    timeout_seconds = 30
    deadline = Time.now + timeout_seconds
    success = false
    while Time.now < deadline
      STDERR.puts 'now perform another RPC and expect OK...'
      begin
        stub.echo(Echo::EchoRequest.new(request: 'hello'), deadline: Time.now + 10)
        success = true
        break
      rescue GRPC::BadStatus => e
        STDERR.puts "RPC received status: #{e}. Try again..."
      end
    end
    unless success
      fail "failed to complete a successful RPC within #{timeout_seconds} seconds"
    end
  end
end

# rubocop:disable Metrics/MethodLength
def main
  server_runner = ServerRunner.new(EchoServerImpl)
  server_runner.server_creds = create_server_creds
  server_port = server_runner.run
  channel_args = {
    GRPC::Core::Channel::SSL_TARGET => 'foo.test.google.fr'
  }
  token_fetch_attempts = MutableValue.new(0)
  token_fetch_attempts_mu = Mutex.new
  jwt_aud_uri_extraction_success_count = MutableValue.new(0)
  jwt_aud_uri_extraction_success_count_mu = Mutex.new
  expected_jwt_aud_uri = 'https://foo.test.google.fr/echo.EchoServer'
  jwt_aud_uri_failure_values = []
  times_out_first_time_auth_proc = proc do |args|
    # We check the value of jwt_aud_uri not necessarily as a test for
    # the correctness of jwt_aud_uri w.r.t. its expected semantics, but
    # more for as an indirect way to check for memory corruption.
    jwt_aud_uri_extraction_success_count_mu.synchronize do
      if args[:jwt_aud_uri] == expected_jwt_aud_uri
        jwt_aud_uri_extraction_success_count.value += 1
      else
        jwt_aud_uri_failure_values << args[:jwt_aud_uri]
      end
    end
    token_fetch_attempts_mu.synchronize do
      old_val = token_fetch_attempts.value
      token_fetch_attempts.value += 1
      if old_val.zero?
        STDERR.puts 'call creds plugin sleeping for 4 seconds'
        sleep 4
        STDERR.puts 'call creds plugin done with 4 second sleep'
        raise 'test exception thrown purposely from call creds plugin'
      end
    end
    { 'authorization' => 'fake_val' }.merge(args)
  end
  channel_creds = create_channel_creds.compose(
    GRPC::Core::CallCredentials.new(times_out_first_time_auth_proc))
  stub = Echo::EchoServer::Stub.new("localhost:#{server_port}",
                                    channel_creds,
                                    channel_args: channel_args)
  STDERR.puts 'perform a first few RPCs to try to get things into a bad state...'
  threads = []
  got_at_least_one_failure = MutableValue.new(false)
  2000.times do
    threads << Thread.new do
      begin
        # 2 seconds is chosen as deadline here because it is less than the 4 second
        # sleep that the first call creds user callback does. The idea here is that
        # a lot of RPCs will be made concurrently all with 2 second deadlines, and they
        # will all queue up onto the call creds user callback thread, and will all
        # have to wait for the first 4 second sleep to finish. When the deadlines
        # of the associated calls fire ~2 seconds in, some of their C-core data
        # will have ownership dropped, and they will hit the user-after-free in
        # https://github.com/grpc/grpc/issues/19195 if this isn't handled correctly.
        stub.echo(Echo::EchoRequest.new(request: 'hello'), deadline: Time.now + 2)
      rescue GRPC::BadStatus
        got_at_least_one_failure.value = true
        # We don't care if these RPCs succeed or fail. The purpose of these
        # RPCs is just to try to induce a specific use-after-free bug, and to get
        # the call credentials callback thread into a bad state.
      end
    end
  end
  threads.each(&:join)
  unless got_at_least_one_failure.value
    fail 'expected at least one of the initial RPCs to fail'
  end
  check_rpcs_still_possible(stub)
  jwt_aud_uri_extraction_success_count_mu.synchronize do
    if jwt_aud_uri_extraction_success_count.value < 4
      fail "Expected auth metadata plugin callback to be ran with the jwt_aud_uri
parameter matching its expected value at least 4 times (at least 1 out of the 2000
initial expected-to-timeout RPCs should have caused this by now, and all three of the
successful RPCs should have caused this). This test isn't doing what it's meant to do."
    end
    unless jwt_aud_uri_failure_values.empty?
      fail "Expected to get jwt_aud_uri:#{expected_jwt_aud_uri} passed to call creds
user callback every time that it was invoked, but it did not match the expected value
in #{jwt_aud_uri_failure_values.size} invocations. This suggests that either:
a) the expected jwt_aud_uri value is incorrect
b) there is some corruption of the jwt_aud_uri argument
Here are are the values of the jwt_aud_uri parameter that were passed to the call
creds user callback that did not match #{expected_jwt_aud_uri}:
#{jwt_aud_uri_failure_values}"
    end
  end
  server_runner.stop
end

main
