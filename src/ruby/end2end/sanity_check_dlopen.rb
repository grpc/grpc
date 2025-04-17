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

# Some tests are flaking by failing to dlopen grpc. Perform some sanity checks.
this_dir = File.expand_path(File.dirname(__FILE__))
grpc_bin_dir = File.join(File.join(File.dirname(this_dir), 'lib'), 'grpc')
grpc_c_sha256_path = File.join(grpc_bin_dir, 'grpc_c_sha256')
grpc_c_so_path = if RUBY_PLATFORM =~ /darwin/
                   File.join(grpc_bin_dir, 'grpc_c.bundle')
                 else
                   File.join(grpc_bin_dir, 'grpc_c.so')
                 end

require 'digest'

# first try to detect corruption b/t the build and now
actual_sha256 = Digest::SHA256.file(grpc_c_so_path).hexdigest
expected_sha256 = File.read(grpc_c_sha256_path).chomp
raise "detected corruption in #{grpc_c_so_path}: sha256: |#{actual_sha256}| != expected sha256: |#{expected_sha256}|" if actual_sha256 != expected_sha256
STDERR.puts "verified sha256 of #{grpc_c_so_path}"

def try_command(command)
  STDERR.puts "==== run |#{command}| BEGIN ===="
  output = `#{command} || true`
  STDERR.puts output
  STDERR.puts "==== run |#{command}| DONE ===="
end

try_command('vm_stat')
try_command('free')
try_command('ulimit -v')

# sanity check that we can load grpc in a child process, log things like available
# memory on the off chance we might be low.
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
_, status = Process.wait2(pid)
fail "sanity check require grpc in child process FAILED exit code #{status.exitstatus}" unless status.success?
STDERR.puts "==== sanity check require grpc in child process SUCCESS ====="
