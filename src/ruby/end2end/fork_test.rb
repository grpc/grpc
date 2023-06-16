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
  # TODO(apolcyn): set timeout back to 300
  stub.echo(Echo::EchoRequest.new(request: 'hello'), deadline: Time.now + 3)
end

def main
  this_dir = File.expand_path(File.dirname(__FILE__))
  echo_server_path = File.join(this_dir, 'echo_server.rb')
  to_child_r, _to_child_w = IO.pipe
  to_parent_r, to_parent_w = IO.pipe
  # Note gRPC has not yet been initialized, otherwise we would need to call prefork
  # before spawn and postfork_parent after.
  # TODO(apolcyn): consider redirecting server's stderr to a file
  Process.spawn(RbConfig.ruby, echo_server_path, in: to_child_r, out: to_parent_w, err: "server_log")
  to_child_r.close
  to_parent_w.close
  child_port = to_parent_r.gets.strip
  STDERR.puts "server running on port: #{child_port}"
  2.times do
    stub = Echo::EchoServer::Stub.new("localhost:#{child_port}", :this_channel_is_insecure)
    do_rpc(stub)
    STDERR.puts "#{Process.pid} pre_fork begin"
    GRPC.prefork
    STDERR.puts "#{Process.pid} pre_fork done"
    pid = fork do
      $stderr.reopen("child1_log", "w")
      STDERR.puts "#{Process.pid} child1: postfork_child begin"
      GRPC.postfork_child
      STDERR.puts "#{Process.pid} child1: postfork_child done"
      do_rpc(stub)
      STDERR.puts "#{Process.pid} child1: first post-fork RPC done"
      STDERR.puts "#{Process.pid} child1: prefork begin"
      GRPC.prefork
      STDERR.puts "#{Process.pid} child1: prefork done"
      pid2 = fork do
        $stderr.reopen("child2_log", "w")
        STDERR.puts "#{Process.pid} child2: postfork_child begin"
        GRPC.postfork_child
        STDERR.puts "#{Process.pid} child2: postfork_child done"
        do_rpc(stub)
        STDERR.puts "#{Process.pid} child2: first post-fork RPC done"
        do_rpc(stub)
        STDERR.puts "#{Process.pid} child2: second post-fork RPC done"
        STDERR.puts "#{Process.pid} child2: done"
      end
      STDERR.puts "#{Process.pid} child1: postfork_parent begin"
      GRPC.postfork_parent
      STDERR.puts "#{Process.pid} child1: postfork_parent done"
      do_rpc(stub)
      STDERR.puts "#{Process.pid} child1: second post-fork RPC done"
      Process.wait(pid2)
      STDERR.puts "#{Process.pid} child1: done"
    end
    STDERR.puts "parent: postfork_parent begin"
    GRPC.postfork_parent
    STDERR.puts "parent: postfork_parent done"
    do_rpc(stub)
    STDERR.puts "parent: first post-fork RPC done"
    do_rpc(stub)
    STDERR.puts "parent: second post-fork RPC done"
    Process.wait pid
    STDERR.puts "parent: done"
  end
end

main
