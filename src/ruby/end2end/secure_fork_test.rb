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

ENV['GRPC_ENABLE_FORK_SUPPORT'] = "1"
fail "forking only supported on linux" unless RUBY_PLATFORM =~ /linux/

this_dir = File.expand_path(File.dirname(__FILE__))
protos_lib_dir = File.join(this_dir, 'lib')
grpc_lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(grpc_lib_dir) unless $LOAD_PATH.include?(grpc_lib_dir)
$LOAD_PATH.unshift(protos_lib_dir) unless $LOAD_PATH.include?(protos_lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'grpc'
require 'end2end_common'

def do_rpc(stub)
  stub.echo(Echo::EchoRequest.new(request: 'hello'), deadline: Time.now + 300)
end

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

def run_client(stub)
  do_rpc(stub)
  STDERR.puts "GRPC::pre_fork begin"
  GRPC.prefork
  STDERR.puts "GRPC::pre_fork done"
  pid = fork do
    STDERR.puts "child1: GRPC::postfork_child begin"
    GRPC.postfork_child
    STDERR.puts "child1: GRPC::postfork_child done"
    do_rpc(stub)
    STDERR.puts "child1: prefork begin"
    GRPC.prefork
    STDERR.puts "child1: prefork done"
    pid2 = fork do
      STDERR.puts "child2: postfork_child begin"
      GRPC.postfork_child
      STDERR.puts "child2: postfork_child done"
      do_rpc(stub)
      STDERR.puts "child2: first post-fork RPC done"
      do_rpc(stub)
      STDERR.puts "child2: second post-fork RPC done"
      STDERR.puts "child2: done"
    end
    STDERR.puts "child1: postfork_parent begin"
    GRPC.postfork_parent
    STDERR.puts "child1: postfork_parent done"
    STDERR.puts "child: first post-fork RPC done"
    do_rpc(stub)
    STDERR.puts "child1: second post-fork RPC done"
    Process.wait(pid2)
    STDERR.puts "child1: done"
  end
  STDERR.puts "parent: GRPC::postfork_parent begin"
  GRPC.postfork_parent
  STDERR.puts "parent: GRPC::postfork_parent done"
  do_rpc(stub)
  STDERR.puts "parent: first post-fork RPC done"
  do_rpc(stub)
  STDERR.puts "parent: second post-fork RPC done"
  Process.wait pid
  STDERR.puts "parent: done"
end

# This is designed to test fork support around three key things:
# - call credentials (relies on a special background thread)
# - channel credentials (ownership semantics are interesting for re-creating channels)
# - channel args (ownership semantics are interesting for re-creating channels)
def main
  this_dir = File.expand_path(File.dirname(__FILE__))
  echo_server_path = File.join(this_dir, 'echo_server.rb')
  to_child_r, _to_child_w = IO.pipe
  to_parent_r, to_parent_w = IO.pipe
  Process.spawn(RbConfig.ruby, echo_server_path, "--secure", in: to_child_r, out: to_parent_w, err: "server_log")
  to_child_r.close
  to_parent_w.close
  child_port = to_parent_r.gets.strip
  STDERR.puts "server running on port: #{child_port}"
  channel_creds = create_channel_creds.compose(
    GRPC::Core::CallCredentials.new(proc do |args|
      { 'authorization' => 'test' }.merge(args)
    end))
  stub = Echo::EchoServer::Stub.new(
    "localhost:#{child_port}", channel_creds,
    channel_args: { GRPC::Core::Channel::SSL_TARGET => 'foo.test.google.fr' })
  2.times do
    run_client(stub)
  end
end

main
