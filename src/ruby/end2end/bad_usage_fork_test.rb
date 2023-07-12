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
ENV['GRPC_VERBOSITY'] = "DEBUG"
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
  stub.echo(Echo::EchoRequest.new(request: 'hello'), deadline: Time.now + 1)
rescue GRPC::Unavailable => e
  STDERR.puts "RPC terminated with expected error: #{e}"
rescue GRPC::DeadlineExceeded => e
  STDERR.puts "RPC terminated with expected error: #{e}"
end

def expect_error_for(action)
  STDERR.puts "#{action}: begin (pid=#{Process.pid})"
  begin
    yield
  rescue RuntimeError => e
    STDERR.puts "got (expected) error: #{e}"
    STDERR.puts "#{action}: done (pid=#{Process.pid})"
    return
  end
  fail "expected an exception due to: #{action}"
end

def main
  # TODO(apolcyn): point this to a guaranteed-non-listening port
  stub = Echo::EchoServer::Stub.new("localhost:443", :this_channel_is_insecure)
  do_rpc(stub)
  STDERR.puts "GRPC::pre_fork begin"
  t = Thread.new do
    expect_error_for("running prefork in a different thread than gRPC was initialized on") do
      GRPC.prefork
    end
  end
  t.join
  expect_error_for("calling postfork_parent before prefork") { GRPC.postfork_parent }
  expect_error_for("calling postfork_child before prefork") { GRPC.postfork_child }
  with_logging("parent: GRPC.prefork") { GRPC.prefork }
  expect_error_for("calling prefork twice") { GRPC.prefork }
  expect_error_for("calling prefork twice") { GRPC.prefork }
  expect_error_for("using gRPC after prefork") { do_rpc(stub) }
  pid = fork do
    expect_error_for("using gRPC before postfork_child") { do_rpc(stub) }
    expect_error_for("calling postfork_parent from child") { GRPC.postfork_parent }
    with_logging("child: GRPC.postfork_child") { GRPC.postfork_child }
    expect_error_for("calling postfork_child twice") { GRPC.postfork_child }
    with_logging("child: first post-fork RPC") { do_rpc(stub) }
    with_logging("child: second post-fork RPC") { do_rpc(stub) }
    STDERR.puts "child: done"
  end
  expect_error_for("using gRPC before postfork_parent") { do_rpc(stub) }
  expect_error_for("calling postfork_child from parent") { GRPC.postfork_child }
  t = Thread.new do
    expect_error_for("running postfork_parent in a different thread than gRPC was initialized on") do
      GRPC.postfork_parent
    end
  end
  t.join
  with_logging("parent: GRPC.postfork_parent") { GRPC.postfork_parent }
  expect_error_for("calling postfork_parent twice") { GRPC.postfork_parent }
  with_logging("parent: first post-fork RPC") { do_rpc(stub) }
  with_logging("parent: second post-fork RPC") { do_rpc(stub) }
  Process.wait pid
  STDERR.puts "parent: done"
end

main
