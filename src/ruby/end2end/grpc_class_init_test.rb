#!/usr/bin/env ruby

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

require_relative './end2end_common'

def main
  native_grpc_classes = %w( channel
                            server
                            channel_credentials
                            xds_channel_credentials
                            server_credentials
                            xds_server_credentials
                            call_credentials
                            compression_options )

  # there is room for false positives in this test,
  # do a few runs for each config
  4.times do
    native_grpc_classes.each do |grpc_class|
      ['', 'gc', 'concurrency'].each do |stress_test_type|
        STDERR.puts 'start client'
        this_dir = File.expand_path(File.dirname(__FILE__))
        client_path = File.join(this_dir, 'grpc_class_init_client.rb')
        client_pid = Process.spawn(RbConfig.ruby,
                                   client_path,
                                   "--grpc_class=#{grpc_class}",
                                   "--stress_test=#{stress_test_type}")
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
            'It likely freezes when the first constructed gRPC object has ' \
            "type: #{grpc_class}"
        end

        client_exit_code = $CHILD_STATUS
        # concurrency stress test type is expected to exit with a
        # non-zero status due to an exception being raised
        if client_exit_code != 0 && stress_test_type != 'concurrency'
          fail "client failed, exit code #{client_exit_code}"
        end
      end
    end
  end
end

main
