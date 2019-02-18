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
                        'grpc/testing/package_options.proto',
                        "--grpc_out=#{tmp_dir}", # generate the service
                        "--ruby_out=#{tmp_dir}", # generate the definitions
                        "--plugin=protoc-gen-grpc=#{plugin}",
                        chdir: pb_dir,
                        out: File::NULL)

      expect(gen_file).to be_truthy
      begin
        $LOAD_PATH.push(tmp_dir)
        expect { Grpc::Testing::Package::Options::TestService::Service }.to raise_error(NameError)
        expect(require('grpc/testing/package_options_services_pb')).to be_truthy
        expect { Grpc::Testing::Package::Options::TestService::Service }.to_not raise_error
      ensure
        $LOAD_PATH.delete(tmp_dir)
      end
    end
  end
  it 'should refer to message in the correct namespace' do
    fail 'CONFIG env variable unexpectedly unset' unless ENV['CONFIG']
    bins_sub_dir = ENV['CONFIG']

    pb_dir = File.dirname(__FILE__)
    bins_dir = File.join('..', '..', '..', '..', '..', 'bins', bins_sub_dir)

    plugin = File.join(bins_dir, 'grpc_ruby_plugin')
    protoc = File.join(bins_dir, 'protobuf', 'protoc')

    # Generate the service from the proto
    Dir.mktmpdir(nil, File.dirname(__FILE__)) do |tmp_dir|
      gen_file = system(protoc,
                        '-I',
                        'grpc/testing',
                        "--grpc_out=#{tmp_dir}", # generate the service
                        "--ruby_out=#{tmp_dir}", # generate the definitions
                        "--plugin=protoc-gen-grpc=#{plugin}",
                        'messages.proto',
                        'service.proto',
                        chdir: pb_dir,
                        out: File::NULL)

      expect(gen_file).to be_truthy
      begin
        $LOAD_PATH.push(tmp_dir)
        expect { MyService::Request }.to raise_error(NameError)
        expect { MyService::Service }.to raise_error(NameError)
        expect(require('service_services_pb')).to be_truthy
        expect { MyService::Request }.to_not raise_error
        expect { MyService::Service }.to_not raise_error
      ensure
        $LOAD_PATH.delete(tmp_dir)
      end
    end
  end
  it 'should resolve to ruby_package given no proto package' do
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
                        'grpc/testing/no_proto_pkg.proto',
                        "--grpc_out=#{tmp_dir}", # generate the service
                        "--ruby_out=#{tmp_dir}", # generate the definitions
                        "--plugin=protoc-gen-grpc=#{plugin}",
                        chdir: pb_dir,
                        out: File::NULL)

      expect(gen_file).to be_truthy
      begin
        $LOAD_PATH.push(tmp_dir)
        expect { I::Have::No::Proto::Package::TestService::Service }.to raise_error(NameError)
        expect(require('grpc/testing/no_proto_pkg_services_pb.rb')).to be_truthy
        expect { I::Have::No::Proto::Package::TestService::Service }.to_not raise_error
      ensure
        $LOAD_PATH.delete(tmp_dir)
      end
    end
  end
  it 'should resolve to proto package given no ruby_package' do
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
                        'grpc/testing/no_ruby_pkg.proto',
                        "--grpc_out=#{tmp_dir}", # generate the service
                        "--ruby_out=#{tmp_dir}", # generate the definitions
                        "--plugin=protoc-gen-grpc=#{plugin}",
                        chdir: pb_dir,
                        out: File::NULL)

      expect(gen_file).to be_truthy
      begin
        $LOAD_PATH.push(tmp_dir)
        expect { I::Have::No::Ruby::Package::TestService::Service }.to raise_error(NameError)
        expect(require('grpc/testing/no_ruby_pkg_services_pb.rb')).to be_truthy
        expect { I::Have::No::Ruby::Package::TestService::Service }.to_not raise_error
      ensure
        $LOAD_PATH.delete(tmp_dir)
      end
    end
  end
end
