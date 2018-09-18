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

    src_dir = File.join(File.dirname(__FILE__), '..', '..', '..', '..')
    pb_dir = File.join(src_dir, 'proto')
    bins_dir = File.join(src_dir, '..', 'bins', bins_sub_dir)

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
end
