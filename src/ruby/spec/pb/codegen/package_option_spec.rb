# Copyright 2018 gRPC authors.
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

require 'spec_helper'
require 'open3'
require 'tmpdir'

describe 'Code Generation Options' do
  it 'should generate and respect package options' do
    with_protos(%w[grpc/testing/package_options.proto]) do
      expect { Grpc::Testing::Package::Options::TestService::Service }.to raise_error(NameError)
      expect(require('grpc/testing/package_options_services_pb')).to be_truthy
      expect { Grpc::Testing::Package::Options::TestService::Service }.to_not raise_error
      expect { Grpc::Testing::TestService::Service }.to raise_error(NameError)
    end
  end

  it 'should generate and respect Ruby style package options' do
    with_protos(%w[grpc/testing/package_options_ruby_style.proto grpc/testing/package_options_import.proto]) do
      expect { RPC::Test::New::Package::Options::AnotherTestService::Service }.to raise_error(NameError)
      expect(require('grpc/testing/package_options_ruby_style_services_pb')).to be_truthy
      expect { RPC::Test::New::Package::Options::AnotherTestService::Service }.to_not raise_error
      expect { Grpc::Testing::AnotherTestService::Service }.to raise_error(NameError)

      services = RPC::Test::New::Package::Options::AnotherTestService::Service.rpc_descs
      expect(services[:GetTest].input).to eq(RPC::Test::New::Package::Options::AnotherTestRequest)
      expect(services[:GetTest].output).to eq(RPC::Test::New::Package::Options::AnotherTestResponse)
      expect(services[:OtherTest].input).to eq(A::Other::Thing)
      expect(services[:OtherTest].output).to eq(A::Other::Thing)
      expect(services[:FooTest].input).to eq(RPC::Test::New::Package::Options::Foo)
      expect(services[:FooTest].output).to eq(RPC::Test::New::Package::Options::Foo)
    end
  end
end

def with_protos(file_paths)
  fail 'CONFIG env variable unexpectedly unset' unless ENV['CONFIG']
  bins_sub_dir = ENV['CONFIG']

  pb_dir = File.dirname(__FILE__)
  bins_dir = File.join('..', '..', '..', '..', '..', 'bins', bins_sub_dir)

  plugin = File.join(bins_dir, 'grpc_ruby_plugin')
  protoc = File.join(bins_dir, 'protobuf', 'protoc')

  # Generate the service from the proto
  Dir.mktmpdir(nil, File.dirname(__FILE__)) do |tmp_dir|
    gen_file = system(protoc,
                      '-I.',
                      *file_paths,
                      "--grpc_out=#{tmp_dir}", # generate the service
                      "--ruby_out=#{tmp_dir}", # generate the definitions
                      "--plugin=protoc-gen-grpc=#{plugin}",
                      chdir: pb_dir,
                      out: File::NULL)

    expect(gen_file).to be_truthy
    begin
      $LOAD_PATH.push(tmp_dir)
      yield
    ensure
      $LOAD_PATH.delete(tmp_dir)
    end
  end
end
