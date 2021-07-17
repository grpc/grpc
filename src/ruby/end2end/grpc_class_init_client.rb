#!/usr/bin/env ruby

# Copyright 2015 gRPC authors.
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

# For GRPC::Core classes, which use the grpc c-core, object init
# is interesting because it's related to overall library init.

require_relative './end2end_common'

def construct_many(test_proc)
  thds = []
  4.times do
    thds << Thread.new do
      20.times do
        test_proc.call
      end
    end
  end
  20.times do
    test_proc.call
  end
  thds.each(&:join)
end

def run_gc_stress_test(test_proc)
  GC.disable
  construct_many(test_proc)

  GC.enable
  construct_many(test_proc)

  GC.start
  construct_many(test_proc)
end

def run_concurrency_stress_test(test_proc)
  100.times do
    Thread.new do
      test_proc.call
    end
  end

  test_proc.call

  fail '(expected) exception thrown while child thread initing class'
end

# default (no gc_stress and no concurrency_stress)
def run_default_test(test_proc)
  thd = Thread.new do
    test_proc.call
  end
  test_proc.call
  thd.join
end

def get_test_proc(grpc_class)
  case grpc_class
  when 'channel'
    return proc do
      GRPC::Core::Channel.new('phony_host', nil, :this_channel_is_insecure)
    end
  when 'server'
    return proc do
      GRPC::Core::Server.new({})
    end
  when 'channel_credentials'
    return proc do
      GRPC::Core::ChannelCredentials.new
    end
  when 'xds_channel_credentials'
    return proc do
      GRPC::Core::XdsChannelCredentials.new(GRPC::Core::ChannelCredentials.new)
    end
  when 'server_credentials'
    return proc do
      test_root = File.join(File.dirname(__FILE__), '..', 'spec', 'testdata')
      files = ['ca.pem', 'server1.key', 'server1.pem']
      creds = files.map { |f| File.open(File.join(test_root, f)).read }
      GRPC::Core::ServerCredentials.new(
        creds[0],
        [{ private_key: creds[1], cert_chain: creds[2] }],
        true)
    end
  when 'xds_server_credentials'
    return proc do
      test_root = File.join(File.dirname(__FILE__), '..', 'spec', 'testdata')
      files = ['ca.pem', 'server1.key', 'server1.pem']
      creds = files.map { |f| File.open(File.join(test_root, f)).read }
      GRPC::Core::XdsServerCredentials.new(
        GRPC::Core::ServerCredentials.new(
          creds[0],
          [{ private_key: creds[1], cert_chain: creds[2] }],
          true))
    end
  when 'call_credentials'
    return proc do
      GRPC::Core::CallCredentials.new(proc { |noop| noop })
    end
  when 'compression_options'
    return proc do
      GRPC::Core::CompressionOptions.new
    end
  else
    fail "bad --grpc_class=#{grpc_class} param"
  end
end

def main
  grpc_class = ''
  stress_test = ''
  OptionParser.new do |opts|
    opts.on('--grpc_class=P', String) do |p|
      grpc_class = p
    end
    opts.on('--stress_test=P') do |p|
      stress_test = p
    end
  end.parse!

  test_proc = get_test_proc(grpc_class)

  # the different test configs need to be ran
  # in separate processes, since each one tests
  # clean shutdown in a different way
  case stress_test
  when 'gc'
    p 'run gc stress'
    run_gc_stress_test(test_proc)
  when 'concurrency'
    p 'run concurrency stress'
    run_concurrency_stress_test(test_proc)
  when ''
    p 'run default'
    run_default_test(test_proc)
  else
    fail "bad --stress_test=#{stress_test} param"
  end
end

main
