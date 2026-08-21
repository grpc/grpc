#!/usr/bin/env ruby
#
# Copyright 2026 gRPC authors.
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

# Regression test for crash in AresResolver during Process.fork when
# channels are destroyed concurrently with fork's pthread_atfork handlers.
#
# Bug: When a Process._fork hook destroys gRPC channels on a background
# thread while fork() is in progress, the channel destruction (resolver
# Orphan) races with the fork handler's resolver Reset/Restart lifecycle.
# This corrupts resolver state, causing a SEGFAULT or SIGABRT
# (GRPC_CHECK_NE(channel_, nullptr) in AresResolver::~AresResolver).
#
# This simulates a production crash triggered by a Process._fork hook
# that shuts down gRPC resources during fork.
#
# Crashed with SEGFAULT (signal 11) or SIGABRT (signal 6) before the fix.

ENV['GRPC_ENABLE_FORK_SUPPORT'] = "1"
fail "forking only supported on linux" unless RUBY_PLATFORM =~ /linux/
this_dir = File.expand_path(File.dirname(__FILE__))
protos_lib_dir = File.join(this_dir, 'lib')
grpc_lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(grpc_lib_dir) unless $LOAD_PATH.include?(grpc_lib_dir)
$LOAD_PATH.unshift(protos_lib_dir) unless $LOAD_PATH.include?(protos_lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'sanity_check_dlopen'
require 'grpc'
require 'end2end_common'

# Destroy gRPC channels on a background thread concurrently with fork.
# If something shuts down gRPC resources during Process._fork,
# this races with fork's pthread_atfork handlers that manage resolver lifecycle.
module ConcurrentCloseOnForkHook
  def _fork
    t = Thread.new do
      ObjectSpace.each_object(GRPC::Core::Channel) do |ch|
        ch.close
      rescue StandardError
        nil
      end
    end
    pid = super
    begin
      t.kill
    rescue StandardError
      nil
    end
    pid
  end
end
Process.singleton_class.prepend(ConcurrentCloseOnForkHook)

def main
  5.times do |attempt|
    # Create channels with active DNS resolution to populate resolver state
    channels = 20.times.map do |i|
      ch = GRPC::Core::Channel.new(
        "dns:///test-#{attempt}-#{i}-#{rand(100_000)}.example.com:443",
        {},
        :this_channel_is_insecure
      )
      ch.connectivity_state(true)
      ch
    end
    sleep 0.05

    with_logging("attempt #{attempt}: fork") do
      pid = fork { sleep 0.05 }
      _, status = Process.wait2(pid)
      sig = status.termsig
      if sig
        label = case sig
                when 6 then "SIGABRT"
                when 11 then "SEGFAULT"
                else "signal #{sig}"
                end
        fail "child crashed with #{label} during fork with concurrent channel close"
      end
    end

    channels.each do |ch|
      ch.close
    rescue StandardError
      nil
    end
    channels.clear
  end
  STDERR.puts "all forks completed without crash"
end

main
