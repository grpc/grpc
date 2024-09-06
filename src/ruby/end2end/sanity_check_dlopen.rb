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

# Some tests are flaking by failing to dlopen grpc. Perform a sanity check.
pid = fork do
  STDERR.puts "==== sanity check child process BEGIN ===="
  def dump(file_path)
    STDERR.puts "==== dump file: #{file_path} BEGIN ===="
    if File.exist?(file_path)
      File.open(file_path, 'r') do |file|
        file.each_line do |line|
          puts line
        end
      end
    else
      STDERR.puts "file: #{file_path} does not exist"
    end
    STDERR.puts "==== dump file: #{file_path} DONE ===="
  end
  dump("/proc/#{Process.pid}/limits")
  dump("/proc/#{Process.pid}/status")
  STDERR.puts "==== sanity check require grpc in child process BEGIN ====="
  require 'grpc'
  STDERR.puts "==== sanity check require grpc in child process DONE ====="
  dump("/proc/#{Process.pid}/limits")
  dump("/proc/#{Process.pid}/status")
  STDERR.puts "==== sanity check child process DONE ===="
end
Process.wait pid
raise "==== sanity check require grpc in child process FAILED exit code #{$CHILD_STATUS.exitstatus} =====" unless $CHILD_STATUS.success?
STDERR.puts "==== sanity check require grpc in child process SUCCESS ====="
