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
    with_protos(['grpc/testing/package_options_ruby_style.proto',
                 'grpc/testing/package_options_import.proto',
                 'grpc/testing/package_options_import2.proto']) do
      expect { RPC::Test::New::Package::Options::AnotherTestService::Service }.to raise_error(NameError)
      expect(require('grpc/testing/package_options_ruby_style_services_pb')).to be_truthy
      expect { RPC::Test::New::Package::Options::AnotherTestService::Service }.to_not raise_error
      expect { Grpc::Testing::AnotherTestService::Service }.to raise_error(NameError)

      services = RPC::Test::New::Package::Options::AnotherTestService::Service.rpc_descs
      expect(services[:GetTest].input).to eq(RPC::Test::New::Package::Options::AnotherTestRequest)
      expect(services[:GetTest].output).to eq(RPC::Test::New::Package::Options::AnotherTestResponse)
      expect(services[:OtherTest].input).to eq(A::Other::Thing)
      expect(services[:OtherTest].output).to eq(A::Other::Thing)
      expect(services[:PackageTest].input).to eq(A::Other::Thing)
      expect(services[:PackageTest].output).to eq(B::Other::Foo::Bar)
      expect(services[:FooTest].input).to eq(RPC::Test::New::Package::Options::Foo)
      expect(services[:FooTest].output).to eq(RPC::Test::New::Package::Options::Foo)
      expect(services[:NestedMessageTest].input).to eq(RPC::Test::New::Package::Options::Foo)
      expect(services[:NestedMessageTest].output).to eq(RPC::Test::New::Package::Options::Bar::Baz)
    end
  end

  it 'should generate when package and service has same name' do
    with_protos(['grpc/testing/same_package_service_name.proto']) do
      expect { SameName::SameName::Service }.to raise_error(NameError)
      expect(require('grpc/testing/same_package_service_name_services_pb')).to be_truthy
      expect { SameName::SameName::Service }.to_not raise_error
      expect { SameName::Request }.to_not raise_error
      expect { SameName::Status }.to_not raise_error
    end
  end

  it 'should generate when ruby_package and service has same name' do
    with_protos(['grpc/testing/same_ruby_package_service_name.proto']) do
      expect { SameName2::SameName2::Service }.to raise_error(NameError)
      expect(require('grpc/testing/same_ruby_package_service_name_services_pb')).to be_truthy
      expect { SameName2::SameName2::Service }.to_not raise_error
      expect { SameName2::Request }.to_not raise_error
      expect { SameName2::Status }.to_not raise_error
    end
  end
end

def with_protos(file_paths)
  pb_dir = File.dirname(__FILE__)
  bins_dir = File.join('..', '..', '..', '..', '..', 'cmake', 'build')
  plugin = File.join(bins_dir, 'grpc_ruby_plugin')
  protoc = File.join(bins_dir, 'third_party', 'protobuf', 'protoc')

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
