#!/usr/bin/env ruby

# Copyright 2016, Google Inc.
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

require_relative './end2end_common'

def main
  native_grpc_classes = %w( channel
                            server
                            channel_credentials
                            call_credentials
                            compression_options )

  native_grpc_classes.each do |grpc_class|
    STDERR.puts 'start client'
    this_dir = File.expand_path(File.dirname(__FILE__))
    client_path = File.join(this_dir, 'grpc_class_init_client.rb')
    client_pid = Process.spawn(RbConfig.ruby,
                               client_path,
                               "--grpc_class=#{grpc_class}")
    begin
      Timeout.timeout(10) do
        Process.wait(client_pid)
      end
    rescue Timeout::Error
      STDERR.puts "timeout waiting for client pid #{client_pid}"
      Process.kill('SIGKILL', client_pid)
      Process.wait(client_pid)
      STDERR.puts 'killed client child'
      raise 'Timed out waiting for client process. ' \
        'It likely hangs when the first constructed gRPC object has ' \
        "type: #{grpc_class}"
    end

    client_exit_code = $CHILD_STATUS
    fail "client failed, exit code #{client_exit_code}" if client_exit_code != 0
  end
end

main
